// Host-side test for the Moses shm meter writer (dsp/MeterShm.hpp contract).
// Instantiates the REAL MosesPlugin through DPF's PluginExporter (the same
// path the format wrappers use), so it exercises the constructor-time map(),
// the lazy run()-time slot claim, and the per-block publish.
// Uses a private segment via NEXUS_METERS_NAME so it never touches a real
// /nexus-meters.
#include "DistrhoPluginInternal.hpp"
#include "MosesPlugin.hpp"
#include "MeterShm.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

USE_NAMESPACE_DISTRHO;

namespace {

constexpr double   kFs        = 48000.0;
constexpr uint32_t kBlockSize = 512;
constexpr uint32_t kBlocks    = 32;
constexpr uint32_t kFrames    = kBlockSize * kBlocks;

int failures = 0;

void check(bool ok, const char* what) {
    std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
}

// Known signal: 50 Hz (band 1, < 75 Hz) + 1 kHz (band 3, 250..5000 Hz),
// identical on both channels.
void makeInput(std::vector<float>& buf) {
    buf.resize(kFrames);
    for (uint32_t n = 0; n < kFrames; ++n)
        buf[n] = 0.5f  * (float)std::sin(2.0 * M_PI * 50.0   * n / kFs)
               + 0.25f * (float)std::sin(2.0 * M_PI * 1000.0 * n / kFs);
}

// Same band settings on every instance so the on/off renders are comparable:
// thresholds low enough that the 50 Hz / 1 kHz content compresses (GR > 0).
void configureBands(PluginExporter& p) {
    for (int b = 0; b < moses::kNumBands; ++b) {
        const uint32_t base = kParamBand1Base + (uint32_t)b * 7;
        p.setParameterValue(base + kFieldThreshold, -30.f);
        p.setParameterValue(base + kFieldRatio,       4.f);
    }
}

// Render the known signal through the plugin, block by block.
void render(PluginExporter& p, const std::vector<float>& in,
            std::vector<float>& outL, std::vector<float>& outR) {
    outL.assign(kFrames, 0.f);
    outR.assign(kFrames, 0.f);
    for (uint32_t blk = 0; blk < kBlocks; ++blk) {
        const float* ins[2]  = { in.data()   + blk * kBlockSize,
                                 in.data()   + blk * kBlockSize };
        float*       outs[2] = { outL.data() + blk * kBlockSize,
                                 outR.data() + blk * kBlockSize };
        p.run(ins, outs, kBlockSize);
    }
}

} // namespace

int main() {
    using namespace nxmeter;

    // Private segment name (unique per run) so the test is hermetic. Must be
    // set before any plugin is constructed (the segment is mapped in the
    // plugin constructor).
    char name[64];
    std::snprintf(name, sizeof(name), "/moses-nxm-test-%ld", (long)getpid());
    setenv(kNameEnvVar, name, 1);
    ::shm_unlink(name); // in case a previous run died mid-way

    d_nextBufferSize = kBlockSize;
    d_nextSampleRate = kFs;

    std::vector<float> in;
    makeInput(in);

    // --- Reference render: metering OFF (Meter Slot at its -1 default). ---
    std::vector<float> refL, refR;
    {
        PluginExporter plugin(nullptr, nullptr, nullptr, nullptr);
        check(plugin.getParameterCount() == kNumParameters,
              "parameter count includes Meter Slot");
        check(plugin.getParameterValue(kParamMeterSlot) == -1.f,
              "Meter Slot defaults to -1 (off)");
        configureBands(plugin);
        render(plugin, in, refL, refR);
    }

    // The constructor must have mapped (and thus created) the segment even
    // though metering stayed off.
    {
        const int fd = ::shm_open(name, O_RDONLY, 0);
        check(fd >= 0, "segment created by plugin constructor (metering off)");
        if (fd >= 0) {
            const void* base = ::mmap(nullptr, kSegmentSize, PROT_READ,
                                      MAP_SHARED, fd, 0);
            ::close(fd);
            check(base != MAP_FAILED, "reader mmap succeeds (off instance)");
            if (base != MAP_FAILED) {
                const Slot* slots = reinterpret_cast<const Slot*>(
                    static_cast<const unsigned char*>(base) + sizeof(Header));
                bool untouched = true;
                for (uint32_t i = 0; i < kNumSlots; ++i)
                    if (slots[i].frame_counter != 0 || slots[i].flags != 0)
                        untouched = false;
                check(untouched, "no slot claimed/written while Meter Slot = -1");
                ::munmap(const_cast<void*>(base), kSegmentSize);
            }
        }
    }

    // --- Metered render: Meter Slot = 2, same input, same band settings. ---
    std::vector<float> outL, outR;
    {
        PluginExporter plugin(nullptr, nullptr, nullptr, nullptr);
        configureBands(plugin);
        plugin.setParameterValue(kParamMeterSlot, 2.f);
        render(plugin, in, outL, outR);

        // Output audio must be bit-identical with metering on vs off.
        check(std::memcmp(outL.data(), refL.data(), kFrames * sizeof(float)) == 0,
              "output L bit-identical with metering on");
        check(std::memcmp(outR.data(), refR.data(), kFrames * sizeof(float)) == 0,
              "output R bit-identical with metering on");

        // Reader side: independent read-only mapping of the same segment.
        const int fd = ::shm_open(name, O_RDONLY, 0);
        check(fd >= 0, "reader shm_open succeeds");
        if (fd < 0) { ::shm_unlink(name); return EXIT_FAILURE; }
        const void* base = ::mmap(nullptr, kSegmentSize, PROT_READ,
                                  MAP_SHARED, fd, 0);
        ::close(fd);
        check(base != MAP_FAILED, "reader mmap succeeds");
        if (base == MAP_FAILED) { ::shm_unlink(name); return EXIT_FAILURE; }

        const Header* hdr = static_cast<const Header*>(base);
        check(hdr->magic   == kMagic,    "header magic = 'NXMT'");
        check(hdr->version == kVersion,  "header version = 1");
        check(hdr->nslots  == kNumSlots, "header nslots = 32");

        const Slot* slots = reinterpret_cast<const Slot*>(
            static_cast<const unsigned char*>(base) + sizeof(Header));

        const uint32_t counter =
            __atomic_load_n(&slots[2].frame_counter, __ATOMIC_ACQUIRE);
        check((slots[2].flags & kFlagClaimed) != 0, "slot 2 claimed");
        check(counter == kBlocks, "slot 2 frame_counter = one tick per block");

        // values[0..3] = per-band post-comp peaks: finite, >= 0, nonzero for
        // the bands carrying energy (band 1 = 50 Hz, band 3 = 1 kHz).
        bool peaksSane = true;
        for (int b = 0; b < 4; ++b) {
            const float v = slots[2].values[b];
            if (!std::isfinite(v) || v < 0.f || v > 4.f) peaksSane = false;
        }
        check(peaksSane, "values[0..3] finite and in a plausible range");
        check(slots[2].values[0] > 0.01f, "values[0] nonzero (band 1, 50 Hz)");
        check(slots[2].values[2] > 0.01f, "values[2] nonzero (band 3, 1 kHz)");

        // values[4..7] = per-band GR in dB, >= 0; band 1 must be compressing
        // (-6 dBFS sine vs -30 dB threshold, ratio 4).
        bool grSane = true;
        for (int b = 4; b < 8; ++b) {
            const float v = slots[2].values[b];
            if (!std::isfinite(v) || v < 0.f) grSane = false;
        }
        check(grSane, "values[4..7] >= 0 (GR dB)");
        check(slots[2].values[4] > 1.f, "values[4] shows real GR on band 1");

        // Runtime slot change: claim the new slot lazily from run(); the old
        // slot is deliberately NOT unclaimed (readers key on frame_counter).
        plugin.setParameterValue(kParamMeterSlot, 5.f);
        {
            const float* ins[2]  = { in.data(), in.data() };
            float bL[kBlockSize], bR[kBlockSize];
            float* outs[2] = { bL, bR };
            plugin.run(ins, outs, kBlockSize);
        }
        check((slots[5].flags & kFlagClaimed) != 0, "slot 5 claimed after change");
        check(slots[5].frame_counter == 1, "slot 5 counter advanced once");
        check((slots[2].flags & kFlagClaimed) != 0,
              "slot 2 stays claimed after change (no unclaim by design)");
        check(slots[2].frame_counter == kBlocks,
              "slot 2 counter stopped after change");

        plugin.deactivateIfNeeded();
        ::munmap(const_cast<void*>(base), kSegmentSize);
    }

    ::shm_unlink(name);

    if (failures == 0) {
        std::printf("ALL TESTS PASSED\n");
        return EXIT_SUCCESS;
    }
    std::printf("%d TEST(S) FAILED\n", failures);
    return EXIT_FAILURE;
}
