#include "gsf_loader.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include <zlib.h>

#include "vbam/gba/GBA.h"
#include "vbam/gba/Sound.h"

namespace {

const uint32_t kGsfMagic = 0x22465350; // "PSF\x22"
const int kMaxLibs = 11;
const int kFrameTicks = 280896; // GBA CPU cycles per video frame

uint32_t read_u32le(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

struct GsfProgram {
    std::vector<uint8_t> image; // 12-byte header (entry/offset/size) + ROM bytes
    char libname[64];
    std::vector<char> tags;
};

bool get_tag(const std::vector<char> &tags, const char *name, char *out, size_t out_size) {
    const size_t namelen = strlen(name);
    const char *p = tags.data();
    const char *end = p + tags.size();
    while (p < end) {
        const char *eol = (const char *)memchr(p, '\n', end - p);
        size_t linelen = eol ? (size_t)(eol - p) : (size_t)(end - p);
        if (linelen > namelen && memcmp(p, name, namelen) == 0 && p[namelen] == '=') {
            size_t valen = linelen - namelen - 1;
            if (valen >= out_size)
                valen = out_size - 1;
            memcpy(out, p + namelen + 1, valen);
            out[valen] = 0;
            // Strip a trailing carriage return.
            if (valen > 0 && out[valen - 1] == '\r')
                out[valen - 1] = 0;
            return true;
        }
        p = eol ? eol + 1 : end;
    }
    return false;
}

// Parses one .gsf/.minigsf/.gsflib file; libnum selects the _lib tag to read.
bool parse_gsf(const uint8_t *data, size_t size, int libnum, GsfProgram *out) {
    if (size < 16 || read_u32le(data) != kGsfMagic)
        return false;
    const uint32_t reserved_size = read_u32le(data + 4);
    const uint32_t program_size = read_u32le(data + 8);
    const uint32_t program_crc = read_u32le(data + 12);
    if ((uint64_t)16 + reserved_size + program_size > size)
        return false;
    const uint8_t *program = data + 16 + reserved_size;

    std::vector<uint8_t> image;
    if (program_size > 0) {
        if (crc32(crc32(0L, Z_NULL, 0), program, program_size) != program_crc)
            return false;
        // Inflate with a growing buffer; GSF programs are a few MB at most.
        uLongf inflated = program_size * 4 + 16;
        std::vector<uint8_t> buf(inflated);
        int ret = Z_BUF_ERROR;
        while (ret == Z_BUF_ERROR && inflated < 64 * 1024 * 1024) {
            inflated *= 2;
            buf.resize(inflated);
            uLongf dest = inflated;
            ret = uncompress(buf.data(), &dest, program, program_size);
            if (ret == Z_OK)
                buf.resize(dest);
        }
        if (ret != Z_OK || buf.size() < 12)
            return false;
        image = std::move(buf);
    }

    const uint8_t *tag_base = program + program_size;
    const size_t tag_avail = size - (tag_base - data);
    out->tags.clear();
    if (tag_avail >= 5 && memcmp(tag_base, "[TAG]", 5) == 0) {
        const size_t tag_len = tag_avail - 5 > 50000 ? 50000 : tag_avail - 5;
        out->tags.assign((const char *)tag_base + 5, (const char *)tag_base + 5 + tag_len);
    }

    out->image = std::move(image);
    out->libname[0] = 0;
    char tag[16];
    snprintf(tag, sizeof(tag), libnum == 1 ? "_lib" : "_lib%d", libnum);
    get_tag(out->tags, tag, out->libname, sizeof(out->libname));
    return true;
}

} // namespace

struct ScopeSink : public GBASoundOut {
    std::vector<int16_t> samples;
    void write(const void *data, unsigned long bytes) override {
        const int16_t *s = (const int16_t *)data;
        samples.insert(samples.end(), s, s + bytes / 2);
    }
};

struct viogsf_s {
    GBASystem *gba = nullptr;
    ScopeSink sink;
    std::vector<uint8_t> rom_image;
    uint32_t entry = 0;
    bool multiboot = false;
    size_t surplus = 0; // consumed prefix of sink.samples

    bool boot() {
        CPUCleanUp(gba);
        gba->cpuIsMultiBoot = multiboot;
        const size_t body = rom_image.size() - 12;
        if (multiboot && body > 0x40000)
            return false;
        if (CPULoadRom(gba, rom_image.data() + 12, (u32)body) == 0)
            return false;
        CPUInit(gba);
        CPUReset(gba);
        soundReset(gba);
        sink.samples.clear();
        surplus = 0;
        return true;
    }
};

static void release_file(const viogsf_reader_t *reader, uint8_t *data) {
    if (!data)
        return;
    if (reader->free_file)
        reader->free_file(data, reader->ctx);
    else
        free(data);
}

viogsf_t *viogsf_open(const uint8_t *data, size_t size,
                      const viogsf_reader_t *reader, long sample_rate) {
    GsfProgram song;
    if (!parse_gsf(data, size, 1, &song))
        return nullptr;

    // Resolve the _lib chain, deepest last; merged image starts at the first _lib.
    std::vector<GsfProgram> chain;
    chain.push_back(std::move(song));
    int libnum = 1;
    for (; (int)chain.size() <= kMaxLibs; ++libnum) {
        char want[16];
        snprintf(want, sizeof(want), libnum == 1 ? "_lib" : "_lib%d", libnum);
        const GsfProgram *owner = nullptr;
        for (const auto &prog : chain) {
            char probe[64];
            if (get_tag(prog.tags, want, probe, sizeof(probe))) {
                owner = &prog;
                break;
            }
        }
        if (!owner)
            break;
        char libname[64];
        get_tag(owner->tags, want, libname, sizeof(libname));
        if (!reader || !reader->read_file)
            break;
        uint8_t *libdata = nullptr;
        size_t libsize = 0;
        if (reader->read_file(libname, &libdata, &libsize, reader->ctx) != 0)
            break;
        GsfProgram lib;
        const bool ok = parse_gsf(libdata, libsize, libnum + 1, &lib);
        release_file(reader, libdata);
        if (!ok)
            return nullptr;
        chain.push_back(std::move(lib));
    }

    // Every declared _lib must have resolved; otherwise the image is incomplete.
    {
        char want[16];
        snprintf(want, sizeof(want), libnum == 1 ? "_lib" : "_lib%d", libnum);
        for (const auto &prog : chain) {
            char probe[64];
            if (get_tag(prog.tags, want, probe, sizeof(probe)))
                return nullptr;
        }
    }

    // Merge: first _lib's image is the base; every other program overlays at its offset.
    GsfProgram *base = &chain[0];
    size_t first_lib = 0;
    if (chain[0].libname[0] != 0 && chain.size() > 1) {
        base = &chain[1];
        first_lib = 1;
    }
    if (base->image.size() < 12)
        return nullptr;
    for (size_t i = 0; i < chain.size(); ++i) {
        if (i == first_lib || chain[i].image.size() < 12)
            continue;
        const uint32_t offset = read_u32le(chain[i].image.data() + 4) & 0x01FFFFFF;
        const uint32_t chunk = read_u32le(chain[i].image.data() + 8);
        if ((uint64_t)12 + offset + chunk > base->image.size())
            return nullptr;
        memcpy(base->image.data() + 12 + offset, chain[i].image.data() + 12, chunk);
    }

    viogsf_t *player = new (std::nothrow) viogsf_t();
    if (!player)
        return nullptr;
    player->rom_image = std::move(base->image);
    player->entry = read_u32le(player->rom_image.data());
    player->multiboot = (player->entry >> 24) == 0x02;

    player->gba = new (std::nothrow) GBASystem();
    if (!player->gba) {
        delete player;
        return nullptr;
    }
    if (!soundInit(player->gba, &player->sink)) {
        delete player->gba;
        delete player;
        return nullptr;
    }
    soundSetSampleRate(player->gba, sample_rate);
    if (!player->boot()) {
        soundShutdown(player->gba);
        delete player->gba;
        delete player;
        return nullptr;
    }
    return player;
}

int viogsf_render(viogsf_t *player, int16_t *out_stereo, int frames) {
    int done = 0;
    while (done < frames) {
        size_t avail = 0;
        if (player->surplus < player->sink.samples.size())
            avail = (player->sink.samples.size() - player->surplus) / 2;
        if (avail == 0) {
            player->sink.samples.clear();
            player->surplus = 0;
            CPULoop(player->gba, kFrameTicks);
            continue;
        }
        size_t take = avail;
        if (take > (size_t)(frames - done))
            take = frames - done;
        memcpy(out_stereo + done * 2, player->sink.samples.data() + player->surplus, take * 4);
        player->surplus += take * 2;
        done += (int)take;
    }
    return done;
}

void viogsf_reset(viogsf_t *player) {
    if (player)
        player->boot();
}

void viogsf_close(viogsf_t *player) {
    if (!player)
        return;
    soundShutdown(player->gba);
    CPUCleanUp(player->gba);
    delete player->gba;
    delete player;
}

void viogsf_set_scope_callback(viogsf_t *player, viogsf_scope_cb_t callback, void *user) {
    if (!player || !player->gba)
        return;
    soundScopeSetCallback(player->gba, callback, user);
}

void viogsf_set_voice_mute(viogsf_t *player, int voice, int muted) {
    if (!player || !player->gba)
        return;
    soundScopeSetVoiceMute(player->gba, voice, muted);
}

void viogsf_clear_voice_mutes(viogsf_t *player) {
    if (!player || !player->gba)
        return;
    soundScopeClearVoiceMutes(player->gba);
}
