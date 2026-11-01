#include "bridge.h"
#include "protocol.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <locale>
#include <map>
#include <random>
#include <sstream>
#include <vector>
#ifndef OPENNEOUA_AGENT_FORK
#define OPENNEOUA_AGENT_FORK "openneoua"
#endif
#ifndef OPENNEOUA_AGENT_REVISION
#define OPENNEOUA_AGENT_REVISION "unknown"
#endif
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <limits.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace Agent {
namespace {
bool enabled = false, pending = false, held = false;
bool responseDirty = false;
std::string directory, session, lastRequest, cachedResponse;
std::string completionError;
uint64_t frame = 0, acknowledgeFrame = 0, nextPoll = 0, temporarySerial = 0;
uint64_t appliedFrame = 0;
uint64_t capturedFrame = 0;
uint32_t capturedRequest = 0;
int capturedWidth = 0, capturedHeight = 0;
std::string capturedPixels;
bool captureSucceeded = false;
uint32_t envelopeType = static_cast<uint32_t>(-1);
int envelopeSerial = 0;
char envelopeTag;
struct Envelope { SDL_Event event; bool agent; uint32_t request; };
std::map<int, Envelope> envelopes;
Command current, heldCommand;
Sequence sequence;
Hold hold;
TextPacer textPacer;
Snapshot latest;
#ifdef _WIN32
const char *platformName = "windows";
#elif defined(__linux__)
const char *platformName = "linux";
#else
const char *platformName = "other";
#endif
#if defined(__x86_64__) || defined(_M_X64)
const char *architecture = "x86_64";
#elif defined(__aarch64__) || defined(_M_ARM64)
const char *architecture = "aarch64";
#else
const char *architecture = "other";
#endif
#ifdef _WIN32
HANDLE directoryLock = INVALID_HANDLE_VALUE;
#else
int directoryLock = -1;
#endif
uint64_t Now() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}
uint64_t UtcNow() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
}
std::string Path(const std::string &name) { return directory + "/" + name; }
bool Exists(const std::string &path) {
#ifdef _WIN32
    return GetFileAttributesA(path.c_str()) != INVALID_FILE_ATTRIBUTES;
#else
    struct stat st;
    return lstat(path.c_str(), &st) == 0;
#endif
}
bool ResolveDirectory(const char *input) {
#ifdef _WIN32
    if (!input || !(std::strlen(input) > 2 && input[1] == ':' && (input[2] == '/' || input[2] == '\\'))) return false;
    char buffer[32768];
    const DWORD n = GetFullPathNameA(input, sizeof(buffer), buffer, nullptr);
    if (!n || n >= sizeof(buffer)) return false;
    const DWORD attrs = GetFileAttributesA(buffer);
    if (attrs == INVALID_FILE_ATTRIBUTES || !(attrs & FILE_ATTRIBUTE_DIRECTORY) || (attrs & FILE_ATTRIBUTE_REPARSE_POINT)) return false;
    directory = buffer;
    std::replace(directory.begin(), directory.end(), '\\', '/');
#else
    if (!input || input[0] != '/') return false;
    char buffer[PATH_MAX];
    if (!realpath(input, buffer)) return false;
    struct stat st;
    if (lstat(input, &st) || !S_ISDIR(st.st_mode) || S_ISLNK(st.st_mode)) return false;
    directory = buffer;
#endif
    while (directory.size() > 1 && directory.back() == '/') directory.pop_back();
    // Refuse source checkout ancestry. Runtime/capture trees stay external.
    for (std::string parent = directory; !parent.empty();) {
        if (Exists(parent + "/.git")) return false;
#ifdef _WIN32
        if (GetFileAttributesA(parent.c_str()) & FILE_ATTRIBUTE_REPARSE_POINT) return false;
#endif
        const auto slash = parent.find_last_of('/');
        if (slash == std::string::npos || slash == 0) break;
        parent.resize(slash);
    }
    return true;
}
bool LockDirectory() {
#ifdef _WIN32
    directoryLock = CreateFileA(Path("engine.lock").c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
        OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (directoryLock == INVALID_HANDLE_VALUE) return false;
    BY_HANDLE_FILE_INFORMATION info;
    return GetFileInformationByHandle(directoryLock, &info) && !(info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT);
#else
    directoryLock = open(Path("engine.lock").c_str(), O_CREAT | O_RDWR | O_NOFOLLOW, 0600);
    struct stat st;
    return directoryLock >= 0 && !fstat(directoryLock, &st) && S_ISREG(st.st_mode) &&
           flock(directoryLock, LOCK_EX | LOCK_NB) == 0;
#endif
}
bool WriteAtomic(const char *name, const std::string &data) {
    const std::string temporary = Path(std::string(name) + "." + session + "." + std::to_string(++temporarySerial) + ".tmp");
    const std::string destination = Path(name);
    bool good = false;
#ifdef _WIN32
    HANDLE file = CreateFileA(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    good = WriteFile(file, data.data(), static_cast<DWORD>(data.size()), &written, nullptr) && written == data.size();
    if (good) good = FlushFileBuffers(file) != 0;
    CloseHandle(file);
    if (good) good = MoveFileExA(temporary.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
    if (!good) DeleteFileA(temporary.c_str());
#else
    int file = open(temporary.c_str(), O_CREAT | O_EXCL | O_WRONLY | O_NOFOLLOW, 0600);
    if (file < 0) return false;
    size_t offset = 0;
    while (offset < data.size()) {
        const ssize_t n = write(file, data.data() + offset, data.size() - offset);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) break;
        offset += static_cast<size_t>(n);
    }
    good = offset == data.size();
    if (close(file)) good = false;
    if (good) good = rename(temporary.c_str(), destination.c_str()) == 0;
    if (!good) unlink(temporary.c_str());
#endif
    return good;
}
bool ReadRequest(std::string &result) {
    char buffer[4097];
    size_t size = 0;
#ifdef _WIN32
    HANDLE file = CreateFileA(Path("request.txt").c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    BY_HANDLE_FILE_INFORMATION info;
    DWORD read = 0;
    const bool good = GetFileType(file) == FILE_TYPE_DISK && GetFileInformationByHandle(file, &info) &&
        !(info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) &&
        ReadFile(file, buffer, sizeof(buffer), &read, nullptr);
    CloseHandle(file);
    if (!good) return false;
    size = read;
#else
    int file = open(Path("request.txt").c_str(), O_RDONLY | O_NONBLOCK | O_NOFOLLOW);
    if (file < 0) return false;
    struct stat st;
    if (fstat(file, &st) || !S_ISREG(st.st_mode)) { close(file); return false; }
    while (size < sizeof(buffer)) {
        const ssize_t n = read(file, buffer + size, sizeof(buffer) - size);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) break;
        size += static_cast<size_t>(n);
    }
    close(file);
#endif
    if (!size || size > 4096) return false;
    result.assign(buffer, size);
    return true;
}
uint32_t WindowId() {
    SDL_Window *window = SDL_GL_GetCurrentWindow();
    return window ? SDL_GetWindowID(window) : 0;
}
bool Queue(const SDL_Event &event, bool agent = true) {
    if (envelopes.size() >= 16 || envelopeType == static_cast<uint32_t>(-1)) return false;
    if (envelopeSerial == 2147483647) envelopeSerial = 0;
    const int serial = ++envelopeSerial;
    envelopes.emplace(serial, Envelope{event, agent, current.id});
    SDL_Event wrapper = {};
    wrapper.type = envelopeType;
    wrapper.user.code = serial;
    wrapper.user.data1 = &envelopeTag;
    if (SDL_PushEvent(&wrapper) == 1) return true;
    envelopes.erase(serial);
    return false;
}
void DiscardQueuedAgentEvents() {
    for (auto i = envelopes.begin(); i != envelopes.end();) {
        if (i->second.agent) i = envelopes.erase(i); else ++i;
    }
}
SDL_Event KeyEvent(const Command &command, bool down) {
    SDL_Event event = {};
    event.type = down ? SDL_KEYDOWN : SDL_KEYUP;
    event.key.windowID = WindowId();
    event.key.state = down ? SDL_PRESSED : SDL_RELEASED;
    event.key.keysym.scancode = static_cast<SDL_Scancode>(command.a);
    event.key.keysym.sym = SDL_GetKeyFromScancode(event.key.keysym.scancode);
    return event;
}
SDL_Event ButtonEvent(const Command &command, bool down) {
    SDL_Event event = {};
    event.type = down ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
    event.button.windowID = WindowId();
    event.button.state = down ? SDL_PRESSED : SDL_RELEASED;
    event.button.button = static_cast<uint8_t>(command.a);
    event.button.clicks = 1;
    event.button.x = command.b; event.button.y = command.c;
    return event;
}
void ReleaseHeld() {
    if (held) {
        const bool released = Queue(heldCommand.op == Op::Key ? KeyEvent(heldCommand, false) : ButtonEvent(heldCommand, false));
        if (!released) return; // Retry after normal dispatch drains the event queue.
    }
    held = false;
    hold.Stop();
}
bool SendTextCharacter() {
    char character = 0;
    if (!textPacer.Next(frame, character)) return true;
    SDL_Event event = {};
    event.type = SDL_TEXTINPUT; event.text.windowID = WindowId();
    event.text.text[0] = character;
    acknowledgeFrame = frame + 1;
    return Queue(event);
}
bool Coordinates(Command &command) {
    SDL_Window *window = SDL_GL_GetCurrentWindow();
    if (!window) return false;
    int w = 0, h = 0, dw = 0, dh = 0;
    SDL_GetWindowSize(window, &w, &h);
    SDL_GL_GetDrawableSize(window, &dw, &dh);
    const int x = command.op == Op::Move ? command.a : command.b;
    const int y = command.op == Op::Move ? command.b : command.c;
    if (dw <= 0 || dh <= 0 || x >= dw || y >= dh) return false;
    if (command.op == Op::Move) {
        command.a = x * w / dw; command.b = y * h / dh;
        command.c = command.c * w / dw; command.d = command.d * h / dh;
    } else { command.b = x * w / dw; command.c = y * h / dh; }
    return true;
}
bool Dispatch(Command command) {
    if ((command.op == Op::Move || command.op == Op::Click) && !Coordinates(command)) return false;
    if (command.op == Op::Click) {
        // Legacy menus track their pointer in OnMouseMove, not OnMouseDown.
        // Position it before the press without inventing relative camera input.
        SDL_Event motion = {};
        motion.type = SDL_MOUSEMOTION; motion.motion.windowID = WindowId();
        motion.motion.x = command.b; motion.motion.y = command.c;
        if (!Queue(motion)) return false;
    }
    if (command.op == Op::Key || command.op == Op::Click) {
        heldCommand = command;
        held = Queue(command.op == Op::Key ? KeyEvent(command, true) : ButtonEvent(command, true));
        if (held) hold.Start(Now(), frame, command.ms);
        return held;
    }
    if (command.op == Op::Move) {
        SDL_Event event = {};
        event.type = SDL_MOUSEMOTION; event.motion.windowID = WindowId();
        event.motion.x = command.a; event.motion.y = command.b;
        event.motion.xrel = command.c; event.motion.yrel = command.d;
        return Queue(event);
    }
    if (command.op == Op::Text) {
        textPacer.Start(command.text);
        return SendTextCharacter();
    }
    return true;
}
bool Screenshot(std::string &data, int &width, int &height) {
    SDL_Window *window = SDL_GL_GetCurrentWindow();
    if (!window || !SDL_GL_GetCurrentContext()) return false;
    SDL_GL_GetDrawableSize(window, &width, &height);
    if (width <= 0 || height <= 0 || width > 4096 || height > 4096) return false;
    std::vector<unsigned char> pixels(static_cast<size_t>(width) * height * 3);
    typedef void (APIENTRY *BindObject)(GLenum, GLuint);
    const auto bindBuffer = reinterpret_cast<BindObject>(SDL_GL_GetProcAddress("glBindBuffer"));
    const auto bindFramebuffer = reinterpret_cast<BindObject>(SDL_GL_GetProcAddress("glBindFramebuffer"));
    GLint packBuffer = 0, readFramebuffer = 0;
    if (bindBuffer) {
        glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &packBuffer);
        bindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    }
    if (bindFramebuffer) {
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &readFramebuffer);
        bindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    }
    GLint alignment = 0, rowLength = 0, skipRows = 0, skipPixels = 0, readBuffer = 0;
    glGetIntegerv(GL_PACK_ALIGNMENT, &alignment); glGetIntegerv(GL_PACK_ROW_LENGTH, &rowLength);
    glGetIntegerv(GL_PACK_SKIP_ROWS, &skipRows); glGetIntegerv(GL_PACK_SKIP_PIXELS, &skipPixels);
    glGetIntegerv(GL_READ_BUFFER, &readBuffer);
    glPixelStorei(GL_PACK_ALIGNMENT, 1); glPixelStorei(GL_PACK_ROW_LENGTH, 0);
    glPixelStorei(GL_PACK_SKIP_ROWS, 0); glPixelStorei(GL_PACK_SKIP_PIXELS, 0);
    glReadBuffer(GL_BACK);
    glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
    const GLenum error = glGetError();
    glPixelStorei(GL_PACK_ALIGNMENT, alignment); glPixelStorei(GL_PACK_ROW_LENGTH, rowLength);
    glPixelStorei(GL_PACK_SKIP_ROWS, skipRows); glPixelStorei(GL_PACK_SKIP_PIXELS, skipPixels);
    glReadBuffer(readBuffer);
    if (bindFramebuffer) bindFramebuffer(GL_READ_FRAMEBUFFER, readFramebuffer);
    if (bindBuffer) bindBuffer(GL_PIXEL_PACK_BUFFER, packBuffer);
    if (error != GL_NO_ERROR) return false;
    data = "P6\n" + std::to_string(width) + " " + std::to_string(height) + "\n255\n";
    for (int y = height - 1; y >= 0; --y)
        data.append(reinterpret_cast<const char *>(pixels.data() + static_cast<size_t>(y) * width * 3), width * 3);
    return true;
}
double Finite(double value) { return std::isfinite(value) ? value : 0.0; }
void Respond(const char *error = nullptr, bool screenshot = false) {
    int width = 0, height = 0;
    SDL_Window *window = SDL_GL_GetCurrentWindow();
    if (window) SDL_GL_GetDrawableSize(window, &width, &height);
    if (screenshot) {
        width = capturedWidth; height = capturedHeight;
        if (!captureSucceeded || capturedFrame != frame || capturedRequest != current.id ||
            !WriteAtomic("frame.ppm", capturedPixels)) error = "screenshot_failed";
        capturedPixels.clear();
        captureSucceeded = false;
    }
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << "{\"protocol\":1,\"session\":\"" << session << "\",\"id\":" << current.id
        << ",\"ok\":" << (error ? "false" : "true") << ",\"frame\":" << frame
        << ",\"applied_frame\":" << appliedFrame << ",\"completed_frame\":" << frame
        << ",\"time_ms\":" << Now() << ",\"screen\":\"" << latest.screen << "\",\"level_id\":" << latest.level
        << ",\"owner\":" << latest.owner << ",\"width\":" << width << ",\"height\":" << height;
    if (error) out << ",\"error\":\"" << error << '"';
    if (screenshot && !error) out << ",\"screenshot\":\"frame.ppm\"";
    if (latest.localUnit) out << ",\"local_unit\":{\"id\":" << latest.unitId << ",\"prototype\":" << latest.prototype
        << ",\"owner\":" << latest.unitOwner << ",\"energy\":" << latest.energy
        << ",\"position\":[" << Finite(latest.x) << ',' << Finite(latest.y) << ',' << Finite(latest.z) << "]}";
    if (latest.localHost) out << ",\"host_energy\":" << latest.hostEnergy;
    out << "}\n";
    cachedResponse = out.str();
    responseDirty = !WriteAtomic("response.json", cachedResponse);
    pending = false;
    sequence.Complete();
}
} // namespace

void Init() {
    const char *input = SDL_getenv("OPENNEOUA_AGENT_DIR");
    if (!input || !*input) return;
    if (!ResolveDirectory(input) || !LockDirectory()) {
        std::fprintf(stderr, "Agent bridge disabled: invalid, in-checkout or occupied directory.\n");
        Shutdown(); return;
    }
    try {
        std::random_device random;
        std::ostringstream id;
        id << std::hex << std::setfill('0');
        for (int i = 0; i < 4; ++i) id << std::setw(8) << static_cast<uint32_t>(random());
        session = id.str();
    } catch (...) { Shutdown(); return; }
    if (envelopeType == static_cast<uint32_t>(-1)) envelopeType = SDL_RegisterEvents(1);
    if (envelopeType == static_cast<uint32_t>(-1)) { Shutdown(); return; }
    sequence = Sequence(); pending = held = responseDirty = false; frame = 0;
    lastRequest.clear(); cachedResponse.clear(); nextPoll = 0; completionError.clear(); envelopes.clear();
    enabled = WriteAtomic("session.json", "{\"protocol\":1,\"session\":\"" + session +
        "\",\"protocol_major\":1,\"protocol_minor\":0,\"observation_schema\":1,\"fork\":\"" OPENNEOUA_AGENT_FORK
        "\",\"revision\":\"" OPENNEOUA_AGENT_REVISION "\",\"platform\":\"" + platformName +
        "\",\"architecture\":\"" + architecture + "\",\"capture_available\":" +
        (SDL_GL_GetCurrentWindow() ? "true" : "false") +
        ",\"max_hold_ms\":2000,\"max_request_bytes\":4096,\"coordinates\":\"screenshot_pixels\","
        "\"operations\":[\"observe\",\"key\",\"move\",\"click\",\"text\",\"release\"]}\n");
    if (!enabled) Shutdown();
}
void BeforeEvents() {
    if (!enabled) return;
    const uint64_t now = Now();
    if (held && (current.op == Op::Release || !completionError.empty() || hold.Due(now, frame))) {
        ReleaseHeld(); acknowledgeFrame = frame + 1;
    }
    if (pending && completionError.empty() && current.op == Op::Text && !SendTextCharacter()) Respond("input_dispatch_failed");
    if (now < nextPoll) return;
    nextPoll = now + 25;
    if (responseDirty) {
        responseDirty = !WriteAtomic("response.json", cachedResponse);
        if (responseDirty) return;
    }
    std::string line;
    if (!ReadRequest(line)) {
        if (pending) {
            completionError = "agent_io_failure";
            DiscardQueuedAgentEvents(); ReleaseHeld(); acknowledgeFrame = frame + 1;
        }
        return;
    }
    if (line == lastRequest) return;
    const Parsed parsed = Parse(line, session);
    if (!parsed.addressed || !parsed.command.id) { lastRequest = line; return; }
    const bool expired = parsed.valid && parsed.command.expires <= UtcNow();
    const Decision decision = sequence.Begin(parsed.command.id, parsed.valid && !expired && parsed.command.op == Op::Release);
    if (decision == Decision::Defer) return;
    lastRequest = line;
    if (decision == Decision::Ignore) return;
    if (decision == Decision::Repeat) {
        if (!cachedResponse.empty()) responseDirty = !WriteAtomic("response.json", cachedResponse);
        return;
    }
    if (parsed.valid && parsed.command.op == Op::Release) ReleaseHeld();
    current = parsed.command;
    captureSucceeded = false; capturedPixels.clear();
    completionError.clear(); appliedFrame = 0;
    if (current.op == Op::Observe || current.op == Op::Release) appliedFrame = frame + 1;
    pending = true;
    acknowledgeFrame = frame + 1;
    if (!parsed.valid) { Respond(parsed.error.c_str()); return; }
    if (expired) { Respond("expired"); return; }
    if (!Dispatch(current)) { ReleaseHeld(); Respond("input_dispatch_failed"); }
}
void BeforeSwap() {
    if (!enabled || !pending || current.op != Op::Observe) return;
    capturedFrame = frame + 1;
    capturedRequest = current.id;
    captureSucceeded = false;
    try { captureSucceeded = Screenshot(capturedPixels, capturedWidth, capturedHeight); }
    catch (...) { capturedPixels.clear(); }
}
void AfterFrame(const Snapshot &snapshot) {
    if (!enabled) return;
    ++frame;
    latest = snapshot;
    if (pending && !held && envelopes.empty() && frame >= acknowledgeFrame &&
        (!completionError.empty() || current.op != Op::Text || textPacer.Done()))
        Respond(completionError.empty() ? nullptr : completionError.c_str(), current.op == Op::Observe && completionError.empty());
}
void Shutdown() {
    DiscardQueuedAgentEvents();
    ReleaseHeld();
    enabled = false; pending = false;
#ifdef _WIN32
    if (directoryLock != INVALID_HANDLE_VALUE) { CloseHandle(directoryLock); directoryLock = INVALID_HANDLE_VALUE; }
#else
    if (directoryLock >= 0) { close(directoryLock); directoryLock = -1; }
#endif
}
int EventFilter(void *, SDL_Event *event) {
    if (event->type == envelopeType) {
        if (event->user.data1 != &envelopeTag) return 0;
        const auto found = envelopes.find(event->user.code);
        if (found == envelopes.end()) return 0; // Canceled/flushed envelope has no dangling payload.
        const Envelope value = found->second;
        envelopes.erase(found);
        *event = value.event;
        if (value.agent && value.request == current.id && !appliedFrame) appliedFrame = frame + 1;
        return 1;
    }
    const bool focusLost = event->type == SDL_WINDOWEVENT &&
        (event->window.event == SDL_WINDOWEVENT_FOCUS_LOST || event->window.event == SDL_WINDOWEVENT_CLOSE);
    const bool physical = event->type == SDL_KEYDOWN || event->type == SDL_KEYUP || event->type == SDL_TEXTINPUT ||
        event->type == SDL_MOUSEBUTTONDOWN || event->type == SDL_MOUSEBUTTONUP || event->type == SDL_MOUSEWHEEL ||
        (event->type == SDL_MOUSEMOTION && (event->motion.xrel || event->motion.yrel)) ||
        event->type == SDL_CONTROLLERBUTTONDOWN || event->type == SDL_CONTROLLERBUTTONUP ||
        event->type == SDL_CONTROLLERAXISMOTION || event->type == SDL_JOYBUTTONDOWN || event->type == SDL_JOYBUTTONUP;
    if (!enabled || !pending || (!focusLost && !physical) || current.op == Op::Observe) return 1;
    completionError = focusLost ? "focus_lost" : "human_override";
    DiscardQueuedAgentEvents();
    acknowledgeFrame = frame + 1;
    if (held && Queue(*event, false)) {
        // The release goes through remaining normal handlers now; human input follows it.
        *event = heldCommand.op == Op::Key ? KeyEvent(heldCommand, false) : ButtonEvent(heldCommand, false);
        held = false; hold.Stop();
    } else ReleaseHeld();
    return 1;
}
} // namespace Agent
