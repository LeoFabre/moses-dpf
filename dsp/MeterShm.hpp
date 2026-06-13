#pragma once
// MeterShm.hpp — shared-memory metering contract, layout v1. This header is
// the shared contract, duplicated per plugin (like DenormalGuard.hpp); the
// reference copy lives in nxstrip-dpf. The on-disk layout must never diverge.
// Spec: docs/superpowers/specs/2026-06-13-meter-shm-pipeline-design.md
//
// One fixed-size POSIX shm segment ("/nexus-meters", 4 KiB), created by
// whoever arrives first (plugin or bridge):
//
//   Header  { uint32 magic = 'NXMT' (0x4E584D54); uint32 version = 1;
//             uint32 nslots = 32; uint32 _pad; }
//   Slot[32]{ uint32 frame_counter;  // writer increments after each publish;
//                                    // 0 = never written
//             uint32 flags;          // bit0 = claimed
//             float  values[8];      // v1: [0]=peak L, [1]=peak R,
//                                    // [2..7] reserved (GR later)
//             uint32 _pad[2]; }      // slot = 48 B
//
// Writer protocol (single writer per slot). API divergence from the nxstrip
// reference: map() is split from the slot claim so the segment can be mapped
// unconditionally in the plugin CONSTRUCTOR (non-RT; sushi applies
// initial_state at a moment that is not guaranteed to precede activate(), so
// gating the mapping on activate() loses the pinned slot). claim() is RT-safe
// and may be called lazily from run() when the slot param changes:
//   map()     — non-RT (constructor): shm_open + ftruncate + mmap, idempotent
//               header init. Any failure -> writer stays disabled for the
//               plugin's lifetime (claim()/publish() become no-ops); never
//               throws. munmap in the destructor via close().
//   claim()   — RT-safe: selects the slot and atomically sets its claimed
//               flag (one atomic RMW on mapped memory, no syscall). A slot
//               change at runtime claims the new slot and deliberately does
//               NOT unclaim the old one: unclaiming from RT would buy nothing
//               (readers key on frame_counter movement to detect live slots),
//               and a restart clears stale claims since the running writer
//               re-asserts its own.
//   publish() — RT-safe: relaxed atomic stores of values, then a release
//               increment of frame_counter. No locks, syscalls or allocation.
//   close()   — non-RT (destructor): clear the current claim, munmap.
//               The segment itself is never unlinked (readers outlive us).
// open(slot) = map() + claim(slot) is kept for compatibility with the
// nxstrip activate()-time idiom.
// Reader protocol: mmap read-only, acquire-load frame_counter, read values.
// Tearing across two floats is acceptable for meters.
//
// The segment name is overridable via the NEXUS_METERS_NAME env var (read at
// map() time, non-RT) so host tests can use a private segment.

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#if defined(__unix__) || defined(__APPLE__)
#define NXMETER_HAVE_SHM 1
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#else
#define NXMETER_HAVE_SHM 0
#endif

namespace nxmeter {

constexpr uint32_t kMagic        = 0x4E584D54u; // 'NXMT'
constexpr uint32_t kVersion      = 1u;
constexpr uint32_t kNumSlots     = 32u;
constexpr uint32_t kSlotValues   = 8u;
constexpr uint32_t kFlagClaimed  = 1u << 0;
constexpr size_t   kSegmentSize  = 4096; // fixed 4 KiB file
constexpr const char* kDefaultName = "/nexus-meters";
constexpr const char* kNameEnvVar  = "NEXUS_METERS_NAME";

struct Header {
    uint32_t magic;
    uint32_t version;
    uint32_t nslots;
    uint32_t _pad;
};

struct Slot {
    uint32_t frame_counter;
    uint32_t flags;
    float    values[kSlotValues];
    uint32_t _pad[2];
};

static_assert(sizeof(Header) == 16, "meter shm header must be 16 bytes");
static_assert(sizeof(Slot)   == 48, "meter shm slot must be 48 bytes");
static_assert(offsetof(Header, magic)   == 0,  "layout v1");
static_assert(offsetof(Header, version) == 4,  "layout v1");
static_assert(offsetof(Header, nslots)  == 8,  "layout v1");
static_assert(offsetof(Slot, frame_counter) == 0, "layout v1");
static_assert(offsetof(Slot, flags)         == 4, "layout v1");
static_assert(offsetof(Slot, values)        == 8, "layout v1");
static_assert(sizeof(Header) + kNumSlots * sizeof(Slot) <= kSegmentSize,
              "header + 32 slots must fit the 4 KiB segment");

#if NXMETER_HAVE_SHM

// Single-writer publisher for one slot of the shared meter segment.
// All shm/mmap work happens in map()/close() (non-RT); claim() and publish()
// are RT-safe.
class MeterShmWriter {
public:
    MeterShmWriter() = default;
    ~MeterShmWriter() { close(); }

    MeterShmWriter(const MeterShmWriter&)            = delete;
    MeterShmWriter& operator=(const MeterShmWriter&) = delete;

    // Non-RT (constructor). Maps the segment, creating it if needed, and
    // initializes the header. Does NOT claim a slot. Returns false (writer
    // disabled, claim()/publish() no-op) on any failure.
    bool map() noexcept
    {
        close();

        const char* env  = std::getenv(kNameEnvVar);
        const char* name = (env != nullptr && env[0] != '\0') ? env : kDefaultName;

        const int fd = ::shm_open(name, O_CREAT | O_RDWR, 0666);
        if (fd < 0)
            return false;

        // Size the segment. On macOS ftruncate fails (EINVAL) on an existing
        // shm object, so tolerate the failure as long as the segment is
        // already large enough.
        struct stat st;
        if (::fstat(fd, &st) != 0) { ::close(fd); return false; }
        if (st.st_size < (off_t)kSegmentSize) {
            if (::ftruncate(fd, (off_t)kSegmentSize) != 0) {
                if (::fstat(fd, &st) != 0 || st.st_size < (off_t)kSegmentSize) {
                    ::close(fd);
                    return false;
                }
            }
        }

        void* base = ::mmap(nullptr, kSegmentSize, PROT_READ | PROT_WRITE,
                            MAP_SHARED, fd, 0);
        ::close(fd); // mapping keeps its own reference
        if (base == MAP_FAILED)
            return false;

        base_ = static_cast<unsigned char*>(base);

        // Idempotent header init: every writer stores the same constants, so
        // racing with another opener is harmless.
        Header* hdr = header();
        __atomic_store_n(&hdr->magic,   kMagic,   __ATOMIC_RELAXED);
        __atomic_store_n(&hdr->version, kVersion, __ATOMIC_RELAXED);
        __atomic_store_n(&hdr->nslots,  kNumSlots, __ATOMIC_RELEASE);
        return true;
    }

    bool isMapped() const noexcept { return base_ != nullptr; }

    // RT-safe. Selects `slot` and asserts its claimed flag (single atomic RMW
    // on mapped memory, no syscall). Out-of-range slot or unmapped segment ->
    // publishing disabled until the next valid claim. Does not unclaim a
    // previously claimed slot (see header comment).
    void claim(int slot) noexcept
    {
        if (base_ == nullptr || slot < 0 || slot >= (int)kNumSlots) {
            slot_ = nullptr;
            return;
        }
        slot_ = slotPtr((uint32_t)slot);
        __atomic_fetch_or(&slot_->flags, kFlagClaimed, __ATOMIC_RELEASE);
    }

    // Non-RT. nxstrip-compatible: map (if needed) and claim in one call.
    bool open(int slot) noexcept
    {
        if (slot < 0 || slot >= (int)kNumSlots)
            return false;
        if (base_ == nullptr && !map())
            return false;
        claim(slot);
        return slot_ != nullptr;
    }

    bool isOpen() const noexcept { return slot_ != nullptr; }

    // RT-safe: relaxed stores of up to kSlotValues values, then a release
    // increment of frame_counter (seqlock-lite; we are the only writer).
    void publish(const float* values, uint32_t count) noexcept
    {
        if (slot_ == nullptr)
            return;
        if (count > kSlotValues)
            count = kSlotValues;
        for (uint32_t i = 0; i < count; ++i)
            storeFloat(&slot_->values[i], values[i]);
        __atomic_fetch_add(&slot_->frame_counter, 1u, __ATOMIC_RELEASE);
    }

    // nxstrip-compatible two-value form (peak L / peak R).
    void publish(float peakL, float peakR) noexcept
    {
        const float v[2] = { peakL, peakR };
        publish(v, 2);
    }

    // Non-RT. Clears the current claim and unmaps (the segment persists).
    void close() noexcept
    {
        if (slot_ != nullptr) {
            __atomic_fetch_and(&slot_->flags, ~kFlagClaimed, __ATOMIC_RELEASE);
            slot_ = nullptr;
        }
        if (base_ != nullptr) {
            ::munmap(base_, kSegmentSize);
            base_ = nullptr;
        }
    }

private:
    Header* header() const noexcept
    {
        return reinterpret_cast<Header*>(base_);
    }

    Slot* slotPtr(uint32_t i) const noexcept
    {
        return reinterpret_cast<Slot*>(base_ + sizeof(Header) + i * sizeof(Slot));
    }

    // Relaxed 32-bit atomic store of a float's bits (the __atomic _n builtins
    // want an integral type; bit-copy keeps it portable across gcc/clang).
    static void storeFloat(float* dst, float v) noexcept
    {
        uint32_t bits;
        std::memcpy(&bits, &v, sizeof(bits));
        __atomic_store_n(reinterpret_cast<uint32_t*>(dst), bits, __ATOMIC_RELAXED);
    }

    unsigned char* base_ = nullptr;
    Slot*          slot_ = nullptr;
};

#else // !NXMETER_HAVE_SHM — non-POSIX host: writer is a permanent no-op.

class MeterShmWriter {
public:
    bool map() noexcept { return false; }
    bool isMapped() const noexcept { return false; }
    void claim(int) noexcept {}
    bool open(int) noexcept { return false; }
    bool isOpen() const noexcept { return false; }
    void publish(const float*, uint32_t) noexcept {}
    void publish(float, float) noexcept {}
    void close() noexcept {}
};

#endif // NXMETER_HAVE_SHM

} // namespace nxmeter
