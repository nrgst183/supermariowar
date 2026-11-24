#include "SkinTransfer.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <system_error>

namespace rollback {

using net::ByteReader;
using net::ByteWriter;
using net::MsgType;

namespace {

namespace fs = std::filesystem;

std::string cleanSkinName(const std::string& name)
{
    std::string out;
    for (char c : name) {
        if (c >= 32 && c < 127 && out.size() < 40)
            out += c;
    }
    while (!out.empty() && out.back() == ' ')
        out.pop_back();
    return out;
}

bool readFileBytes(const std::string& path, std::vector<uint8_t>& bytes)
{
    std::error_code ec;
    const uintmax_t size = fs::file_size(path, ec);
    if (ec || size == 0 || size > SKIN_MAX_BYTES)
        return false;
    std::ifstream file(path, std::ios::binary);
    if (!file)
        return false;
    bytes.resize(static_cast<size_t>(size));
    return static_cast<bool>(file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size)));
}

uint64_t hashOfFile(const std::string& path)
{
    std::vector<uint8_t> bytes;
    return readFileBytes(path, bytes) ? net::fnv1a64(bytes.data(), bytes.size()) : 0;
}

bool isSkinExtension(const fs::path& path)
{
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext == ".png" || ext == ".bmp";
}

} // namespace

std::string safeSkinFileName(const std::string& hint)
{
    std::string base = hint;
    const size_t slash = base.find_last_of("/\\:");
    if (slash != std::string::npos)
        base = base.substr(slash + 1);
    const size_t dot = base.rfind('.');
    if (dot != std::string::npos)
        base = base.substr(0, dot);

    std::string out;
    for (char c : base) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == ' ' || c == '-' || c == '_';
        if (ok && out.size() < 40)
            out += c;
    }
    while (!out.empty() && out.back() == ' ')
        out.pop_back();
    while (!out.empty() && (out.front() == ' ' || out.front() == '-'))
        out.erase(out.begin());
    return out.empty() ? "skin" : out;
}

const char* skinExtensionFor(const std::vector<uint8_t>& bytes)
{
    static const uint8_t png[8] = {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
    if (bytes.size() > 8 && std::equal(png, png + 8, bytes.begin()))
        return ".png";
    if (bytes.size() > 14 && bytes[0] == 'B' && bytes[1] == 'M')
        return ".bmp";
    return "";
}

bool saveDownloadedSkin(const std::string& dir, const std::string& nameHint, const std::vector<uint8_t>& bytes, uint64_t hash, std::string& path,
    std::string& error)
{
    if (bytes.empty() || bytes.size() > SKIN_MAX_BYTES || net::fnv1a64(bytes.data(), bytes.size()) != hash) {
        error = "the skin data doesn't match its hash";
        return false;
    }
    const std::string ext = skinExtensionFor(bytes);
    if (ext.empty()) {
        error = "the skin is not a PNG or BMP image";
        return false;
    }
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (!fs::is_directory(dir, ec)) {
        error = "can't create the folder " + dir;
        return false;
    }

    const std::string base = safeSkinFileName(nameHint);
    fs::path target;
    for (int n = 1; n < 100 && target.empty(); n++) {
        const fs::path candidate = fs::path(dir) / (n == 1 ? base + ext : base + " (" + std::to_string(n) + ")" + ext);
        if (!fs::exists(candidate, ec)) {
            target = candidate;
        } else if (hashOfFile(candidate.string()) == hash) {
            path = candidate.string();
            return true;
        }
    }
    if (target.empty()) {
        error = "too many skins named " + base;
        return false;
    }

    const fs::path part = fs::path(target.string() + ".part");
    {
        std::ofstream file(part, std::ios::binary | std::ios::trunc);
        file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!file) {
            error = "can't write " + part.string();
            file.close();
            fs::remove(part, ec);
            return false;
        }
    }
    fs::rename(part, target, ec);
    if (ec) {
        error = "can't rename " + part.string();
        fs::remove(part, ec);
        return false;
    }
    path = target.string();
    return true;
}

SkinExchange::SkinExchange(Options options, Send send, int localPeer)
    : m_options(std::move(options))
    , m_send(std::move(send))
    , m_localPeer(localPeer)
{}

void SkinExchange::scan() const
{
    if (m_scanned)
        return;
    m_scanned = true;
    std::vector<std::string> dirs = m_options.searchDirs;
    dirs.insert(dirs.end(), m_options.downloadDirs.begin(), m_options.downloadDirs.end());
    for (const std::string& dir : dirs) {
        std::error_code ec;
        if (!fs::is_directory(dir, ec))
            continue;
        for (const fs::directory_entry& entry : fs::directory_iterator(dir, ec)) {
            if (!entry.is_regular_file(ec) || !isSkinExtension(entry.path()))
                continue;
            const uint64_t hash = hashOfFile(entry.path().string());
            if (hash != 0)
                m_known.emplace(hash, entry.path().string());
        }
    }
}

std::string SkinExchange::find(uint64_t hash) const
{
    if (hash == 0)
        return {};
    scan();
    const auto it = m_known.find(hash);
    return it == m_known.end() ? std::string() : it->second;
}

void SkinExchange::setLocal(const std::vector<LocalSkinFile>& skins)
{
    m_local.assign(skins.begin(), skins.begin() + static_cast<std::ptrdiff_t>(std::min<size_t>(skins.size(), SKIN_PLAYERS)));
    m_localRef.clear();
    for (const LocalSkinFile& skin : m_local) {
        SkinRef ref;
        std::vector<uint8_t> bytes;
        if (readFileBytes(skin.path, bytes)) {
            ref.name = cleanSkinName(skin.name);
            ref.hash = net::fnv1a64(bytes.data(), bytes.size());
            ref.size = static_cast<uint32_t>(bytes.size());
            m_known.emplace(ref.hash, skin.path);
        }
        m_localRef.push_back(ref);
    }
    //(skins scrolled past: their uploads stop, not to hold up the one chosen now; the peers ignore them)
    m_uploads.erase(std::remove_if(m_uploads.begin(), m_uploads.end(),
                        [&](const Upload& up) {
                            return std::none_of(m_localRef.begin(), m_localRef.end(), [&](const SkinRef& ref) { return ref.hash == up.hash; });
                        }),
        m_uploads.end());
    m_infoSentMask = 0;
}

void SkinExchange::sendInfo(int peer)
{
    uint8_t buffer[2 + SKIN_PLAYERS * (1 + 40 + 8 + 4) + 8];
    ByteWriter w(buffer, sizeof(buffer));
    w.u8(static_cast<uint8_t>(MsgType::SkinInfo));
    w.u8(static_cast<uint8_t>(m_localRef.size()));
    for (const SkinRef& ref : m_localRef) {
        w.str(ref.name);
        w.u64(ref.hash);
        w.u32(ref.size);
    }
    if (w.ok())
        m_send(peer, w.data(), w.size());
}

void SkinExchange::update(uint32_t nowMs, uint8_t connectedMask)
{
    for (int p = 0; p < net::MAX_PEERS; p++) {
        const uint8_t bit = static_cast<uint8_t>(1u << p);
        if (p == m_localPeer)
            continue;
        if (!(connectedMask & bit)) {
            m_infoSentMask &= static_cast<uint8_t>(~bit);
            continue;
        }
        if (!(m_infoSentMask & bit) && !m_localRef.empty()) {
            m_infoSentMask |= bit;
            sendInfo(p);
        }
    }

    const uint32_t frames = (nowMs - m_lastChunkMs) / 16;
    if (frames == 0 || m_uploads.empty())
        return;
    m_lastChunkMs = nowMs;
    int budget = SKIN_CHUNKS_PER_FRAME * static_cast<int>(std::min<uint32_t>(frames, 4));
    for (Upload& up : m_uploads) {
        if (!(connectedMask & (1u << up.peer))) {
            up.offset = static_cast<uint32_t>(up.file->size());
            continue;
        }
        if (!up.begun && budget > 0) {
            uint8_t buffer[64];
            ByteWriter w(buffer, sizeof(buffer));
            w.u8(static_cast<uint8_t>(MsgType::SkinBegin));
            w.u64(up.hash);
            w.u32(static_cast<uint32_t>(up.file->size()));
            std::string name;
            for (const SkinRef& ref : m_localRef) {
                if (ref.hash == up.hash)
                    name = ref.name;
            }
            w.str(name);
            m_send(up.peer, w.data(), w.size());
            up.begun = true;
        }
        for (; budget > 0 && up.begun && up.offset < up.file->size(); budget--) {
            const size_t len = std::min(SKIN_CHUNK_BYTES, up.file->size() - up.offset);
            uint8_t chunk[SKIN_CHUNK_BYTES + 32];
            ByteWriter w(chunk, sizeof(chunk));
            w.u8(static_cast<uint8_t>(MsgType::SkinChunk));
            w.u64(up.hash);
            w.u32(up.offset);
            w.u16(static_cast<uint16_t>(len));
            w.bytes(up.file->data() + up.offset, len);
            m_send(up.peer, w.data(), w.size());
            up.offset += static_cast<uint32_t>(len);
        }
    }
    m_uploads.erase(std::remove_if(m_uploads.begin(), m_uploads.end(), [](const Upload& up) { return up.offset >= up.file->size(); }), m_uploads.end());
}

void SkinExchange::dropPeer(int peer)
{
    if (peer < 0 || peer >= net::MAX_PEERS)
        return;
    m_remote[static_cast<size_t>(peer)] = Remote();
    m_download[static_cast<size_t>(peer)] = Download();
    m_infoSentMask &= static_cast<uint8_t>(~(1u << peer));
    m_uploads.erase(std::remove_if(m_uploads.begin(), m_uploads.end(), [&](const Upload& up) { return up.peer == peer; }), m_uploads.end());
    m_revision++;
}

bool SkinExchange::handle(int peer, MsgType type, ByteReader& r)
{
    if (peer < 0 || peer >= net::MAX_PEERS || peer == m_localPeer)
        return type == MsgType::SkinInfo || type == MsgType::SkinRequest || type == MsgType::SkinBegin || type == MsgType::SkinChunk;
    switch (type) {
        case MsgType::SkinInfo:
            onInfo(peer, r);
            return true;
        case MsgType::SkinRequest:
            onRequest(peer, r);
            return true;
        case MsgType::SkinBegin:
            onBegin(peer, r);
            return true;
        case MsgType::SkinChunk:
            onChunk(peer, r);
            return true;
        default:
            return false;
    }
}

void SkinExchange::onInfo(int peer, ByteReader& r)
{
    const int n = std::min<int>(r.u8(), SKIN_PLAYERS);
    std::array<SkinRef, SKIN_PLAYERS> skin {};
    for (int i = 0; i < n; i++) {
        skin[static_cast<size_t>(i)].name = cleanSkinName(r.str());
        skin[static_cast<size_t>(i)].hash = r.u64();
        skin[static_cast<size_t>(i)].size = r.u32();
        if (!skin[static_cast<size_t>(i)].valid())
            skin[static_cast<size_t>(i)] = SkinRef();
    }
    if (!r.ok())
        return;
    Remote& remote = m_remote[static_cast<size_t>(peer)];
    for (size_t i = 0; i < SKIN_PLAYERS; i++) {
        if (skin[i].hash != remote.skin[i].hash)
            remote.requested[i] = false;
        remote.skin[i] = skin[i];
    }
    m_revision++;
    requestMissing(peer);
}

void SkinExchange::requestMissing(int peer)
{
    Remote& remote = m_remote[static_cast<size_t>(peer)];
    for (size_t i = 0; i < SKIN_PLAYERS; i++) {
        const SkinRef& ref = remote.skin[i];
        if (!ref.valid() || remote.requested[i] || !find(ref.hash).empty())
            continue;
        remote.requested[i] = true;
        bool dup = false;
        for (size_t j = 0; j < i; j++)
            dup = dup || (remote.skin[j].hash == ref.hash && remote.requested[j]);
        if (dup)
            continue;
        uint8_t buffer[9];
        ByteWriter w(buffer, sizeof(buffer));
        w.u8(static_cast<uint8_t>(MsgType::SkinRequest));
        w.u64(ref.hash);
        m_send(peer, w.data(), w.size());
    }
}

void SkinExchange::onRequest(int peer, ByteReader& r)
{
    const uint64_t hash = r.u64();
    if (!r.ok())
        return;
    for (const Upload& up : m_uploads) {
        if (up.peer == peer && up.hash == hash)
            return;
    }
    for (const LocalSkinFile& skin : m_local) {
        std::vector<uint8_t> bytes;
        if (!readFileBytes(skin.path, bytes) || net::fnv1a64(bytes.data(), bytes.size()) != hash)
            continue;
        Upload up;
        up.peer = peer;
        up.hash = hash;
        up.file = std::make_shared<const std::vector<uint8_t>>(std::move(bytes));
        m_uploads.push_back(std::move(up));
        return;
    }
}

void SkinExchange::onBegin(int peer, ByteReader& r)
{
    const uint64_t hash = r.u64();
    const uint32_t size = r.u32();
    const std::string name = cleanSkinName(r.str());
    if (!r.ok())
        return;
    if (size == 0 || size > SKIN_MAX_BYTES)
        return;
    //Only what we asked for, and only what it announced
    const Remote& remote = m_remote[static_cast<size_t>(peer)];
    bool wanted = false;
    for (size_t i = 0; i < SKIN_PLAYERS; i++)
        wanted = wanted || (remote.requested[i] && remote.skin[i].hash == hash && remote.skin[i].size == size);
    if (!wanted || !find(hash).empty())
        return;
    Download& dl = m_download[static_cast<size_t>(peer)];
    dl = Download();
    dl.active = true;
    dl.hash = hash;
    dl.size = size;
    dl.name = name;
    dl.bytes.reserve(size);
}

void SkinExchange::onChunk(int peer, ByteReader& r)
{
    const uint64_t hash = r.u64();
    const uint32_t offset = r.u32();
    const uint16_t len = r.u16();
    const uint8_t* data = r.bytes(len);
    Download& dl = m_download[static_cast<size_t>(peer)];
    if (!r.ok() || !dl.active || dl.hash != hash)
        return;
    if (offset != dl.bytes.size() || len == 0 || len > SKIN_CHUNK_BYTES || dl.bytes.size() + len > dl.size) {
        dl = Download();
        return;
    }
    dl.bytes.insert(dl.bytes.end(), data, data + len);
    if (dl.bytes.size() < dl.size)
        return;

    std::string path;
    std::string error = "nowhere to save it";
    if (net::fnv1a64(dl.bytes.data(), dl.bytes.size()) != dl.hash) {
        error = "wrong hash";
    } else {
        for (const std::string& dir : m_options.downloadDirs) {
            if (saveDownloadedSkin(dir, dl.name, dl.bytes, dl.hash, path, error))
                break;
            path.clear();
        }
    }
    if (path.empty()) {
        printf("netplay: ERROR: skin download: %s\n", error.c_str());
    } else {
        printf("netplay: saved the skin %s as %s\n", dl.name.c_str(), path.c_str());
        scan();
        m_known[dl.hash] = path;
        m_revision++;
    }
    dl = Download();
}

std::string SkinExchange::pathOf(int peer, int localIndex) const
{
    if (peer < 0 || peer >= net::MAX_PEERS || localIndex < 0 || localIndex >= SKIN_PLAYERS)
        return {};
    if (peer == m_localPeer)
        return static_cast<size_t>(localIndex) < m_local.size() ? m_local[static_cast<size_t>(localIndex)].path : std::string();
    return find(m_remote[static_cast<size_t>(peer)].skin[static_cast<size_t>(localIndex)].hash);
}

bool SkinExchange::downloading(int peer, int localIndex) const
{
    if (peer < 0 || peer >= net::MAX_PEERS || peer == m_localPeer || localIndex < 0 || localIndex >= SKIN_PLAYERS)
        return false;
    const Remote& remote = m_remote[static_cast<size_t>(peer)];
    const SkinRef& ref = remote.skin[static_cast<size_t>(localIndex)];
    return ref.valid() && remote.requested[static_cast<size_t>(localIndex)] && find(ref.hash).empty();
}

} // namespace rollback
