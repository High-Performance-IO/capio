#ifndef CAPIO_LOGGER_JSON_BASE_HPP
#define CAPIO_LOGGER_JSON_BASE_HPP

// Pull in everything this header needs directly so it is self-contained
// regardless of include order.
#include <cstdarg>   // va_list, va_copy, va_end
#include <cstdio>    // snprintf, vsnprintf
#include <cstring>   // strlen
#include <string>    // std::string (used in expandAndEscape)

#include "constants.hpp" // CAPIO_LOG_MAX_MSG_LEN, CAPIO_DEFAULT_LOG_FOLDER, …
#include "syscall.hpp"   // current_time_in_millis (declared in logger.hpp which
                         // includes syscall.hpp; guard against double-include
                         // by relying on header guards in logger.hpp)

/**
 * @file logger_json_base.hpp
 *
 * CRTP mixin that implements the full structured-JSON adapter interface
 * required by TemplateLogger.  Derived adapters only need to supply two
 * I/O primitives as *static* methods:
 *
 *   static void rawWriteBytes(const char *buf, int len);
 *   static void rawWriteStr (const char *buf);           // NUL-terminated
 *
 * All JSON structure, indentation, pending-comma handling, and string
 * escaping live here, shared between PosixLogWriteAdapter and
 * ServerLogWriteAdapter.
 */
template <typename Derived>
struct JsonLogBase {

    // ------------------------------------------------------------------ //
    //  Thread-local JSON state
    // ------------------------------------------------------------------ //

    static thread_local int  nestingDepth;
    static thread_local bool rootArrayOpen;

    // Pending-line buffer: holds the last event/closing-brace WITHOUT its
    // trailing newline.  Flushed with ",\n" when a sibling follows, plain
    // "\n" when the enclosing array closes.  Avoids trailing commas on an
    // append-only stream without any seek-back.
    static thread_local int  pendingLen;
    static thread_local char pendingBuf[CAPIO_LOG_MAX_MSG_LEN * 6 + 256];

    // ------------------------------------------------------------------ //
    //  TemplateLogger adapter interface
    // ------------------------------------------------------------------ //

    static void write(const char * /*legacy*/, const size_t /*legacy*/) {
        // ts_exit is written by writeEpilogue; nothing to do here.
    }

    /**
     * Called for every LOG(...) inside a syscall scope.
     * Flushes the previous pending entry with a comma, then buffers this
     * event object WITHOUT a trailing newline.
     */
    static void printFormatted(unsigned long int timestamp,
                                const char *invoker, const char *file, int line,
                                const char * /*output_template*/,
                                const char *message_format, va_list args) {

        char escaped_args[CAPIO_LOG_MAX_MSG_LEN * 6];
        expandAndEscape(message_format, args, escaped_args,
                        static_cast<int>(sizeof(escaped_args)));

        char escaped_invoker[512];
        jsonEscape(invoker, static_cast<int>(::strlen(invoker)),
                   escaped_invoker, static_cast<int>(sizeof(escaped_invoker)));

        char escaped_file[512];
        jsonEscape(file, static_cast<int>(::strlen(file)),
                   escaped_file, static_cast<int>(sizeof(escaped_file)));

        flushPending(true); // previous sibling gets a comma

        const int indent = indentSize();
        char *p = JsonLogBase<Derived>::pendingBuf;
        for (int i = 0; i < indent; ++i) { *p++ = ' '; }
        p += ::snprintf(p,
                        sizeof(JsonLogBase<Derived>::pendingBuf) - static_cast<size_t>(indent) - 1,
                        "{ \"ts\": %lu, \"invoker\": \"%s\","
                        " \"file\": \"%s\", \"line\": %d, \"args\": \"%s\" }",
                        timestamp, escaped_invoker, escaped_file, line, escaped_args);
        JsonLogBase<Derived>::pendingLen = static_cast<int>(
            p - JsonLogBase<Derived>::pendingBuf);
    }

    /**
     * Called by TemplateLogger constructor (current_log_level == 1).
     *
     * Emits the opening of one syscall object:
     *   {
     *     "invoker":  "...",
     *     "file":     "...",
     *     "line":     N,
     *     "ts_enter": T,
     *     "args":     "...",
     *     "events": [
     */
    static void writeOpening(unsigned long int timestamp,
                              const char *invoker, const char *file, int line,
                              const char *message_format, va_list args) {

        if (!JsonLogBase<Derived>::rootArrayOpen) {
            Derived::rawWriteStr("[\n");
            JsonLogBase<Derived>::rootArrayOpen = true;
            JsonLogBase<Derived>::nestingDepth  = 1;
        }

        char escaped_args[CAPIO_LOG_MAX_MSG_LEN * 6];
        expandAndEscape(message_format, args, escaped_args,
                        static_cast<int>(sizeof(escaped_args)));

        char escaped_invoker[512];
        jsonEscape(invoker, static_cast<int>(::strlen(invoker)),
                   escaped_invoker, static_cast<int>(sizeof(escaped_invoker)));

        char escaped_file[512];
        jsonEscape(file, static_cast<int>(::strlen(file)),
                   escaped_file, static_cast<int>(sizeof(escaped_file)));

        flushPending(true); // flush previous top-level "}" with comma if present

        writeImmediate("{");
        JsonLogBase<Derived>::nestingDepth++;

        writeField   ("\"invoker\"",  "\"%s\",", escaped_invoker);
        writeField   ("\"file\"",     "\"%s\",", escaped_file);
        writeFieldInt("\"line\"",     "%d,",     line);
        writeFieldUL ("\"ts_enter\"", "%lu,",    timestamp);
        writeField   ("\"args\"",     "\"%s\",", escaped_args);

        writeImmediate("\"events\": [");
        JsonLogBase<Derived>::nestingDepth++;

        JsonLogBase<Derived>::pendingLen = 0;
    }

    /**
     * Called by TemplateLogger destructor (current_log_level == 1).
     * Closes the events array, emits ts_exit, and stores the closing "}"
     * as a new pending line so the next sibling gets a leading comma.
     * Receives the timestamp captured at destructor time by TemplateLogger.
     */
    static void writeEpilogue(unsigned long int timestamp) {
        if (JsonLogBase<Derived>::nestingDepth < 2) { return; }

        flushPending(false); // last event — no trailing comma

        JsonLogBase<Derived>::nestingDepth--;
        writeImmediate("],");  // close "events" array with trailing comma for ts_exit

        {
            char buf[64];
            ::snprintf(buf, sizeof(buf), "\"ts_exit\": %lu", timestamp);
            writeImmediate(buf);
        }

        JsonLogBase<Derived>::nestingDepth--;
        char *p = JsonLogBase<Derived>::pendingBuf;
        const int indent = indentSize();
        for (int i = 0; i < indent; ++i) { *p++ = ' '; }
        *p++ = '}';
        JsonLogBase<Derived>::pendingLen = static_cast<int>(
            p - JsonLogBase<Derived>::pendingBuf);
    }

  protected:
    // ------------------------------------------------------------------ //
    //  Pending-line management
    // ------------------------------------------------------------------ //

    static void flushPending(bool withComma) {
        if (JsonLogBase<Derived>::pendingLen <= 0) { return; }
        Derived::rawWriteBytes(JsonLogBase<Derived>::pendingBuf,
                               JsonLogBase<Derived>::pendingLen);
        Derived::rawWriteStr(withComma ? ",\n" : "\n");
        JsonLogBase<Derived>::pendingLen = 0;
    }

    static void writeImmediate(const char *buf, int len = -1) {
        if (len < 0) { len = static_cast<int>(::strlen(buf)); }
        const int indent = indentSize();
        char spaces[65] = {0};
        for (int i = 0; i < indent; ++i) { spaces[i] = ' '; }
        Derived::rawWriteBytes(spaces, indent);
        Derived::rawWriteBytes(buf, len);
        Derived::rawWriteStr("\n");
    }

    static void writeField(const char *key, const char *fmt, const char *val) {
        char tmp[768]; ::snprintf(tmp, sizeof(tmp), fmt, val);
        char buf[1024]; ::snprintf(buf, sizeof(buf), "%s: %s", key, tmp);
        writeImmediate(buf);
    }
    static void writeFieldInt(const char *key, const char *fmt, int val) {
        char tmp[64]; ::snprintf(tmp, sizeof(tmp), fmt, val);
        char buf[128]; ::snprintf(buf, sizeof(buf), "%s: %s", key, tmp);
        writeImmediate(buf);
    }
    static void writeFieldUL(const char *key, const char *fmt, unsigned long val) {
        char tmp[64]; ::snprintf(tmp, sizeof(tmp), fmt, val);
        char buf[128]; ::snprintf(buf, sizeof(buf), "%s: %s", key, tmp);
        writeImmediate(buf);
    }

    static int indentSize() {
        const int n = JsonLogBase<Derived>::nestingDepth * 2;
        return n < 64 ? n : 64;
    }

    // ------------------------------------------------------------------ //
    //  String helpers
    // ------------------------------------------------------------------ //

    static void expandAndEscape(const char *fmt, va_list args,
                                 char *dst, int dst_size) {
        va_list copy;
        va_copy(copy, args);
        const int raw_len = ::vsnprintf(nullptr, 0, fmt, copy);
        va_end(copy);

        std::string raw(static_cast<size_t>(raw_len) + 1, '\0');
        va_copy(copy, args);
        ::vsnprintf(&raw[0], static_cast<size_t>(raw_len) + 1, fmt, copy);
        va_end(copy);

        jsonEscape(raw.c_str(), raw_len, dst, dst_size);
    }

    static void jsonEscape(const char *src, int src_len,
                            char *dst, int dst_size) {
        int di = 0;
        for (int si = 0; si < src_len && di + 7 < dst_size; ++si) {
            const unsigned char c = static_cast<unsigned char>(src[si]);
            if (c == '"' || c == '\\') {
                dst[di++] = '\\'; dst[di++] = static_cast<char>(c);
            } else if (c == '\n') { dst[di++] = '\\'; dst[di++] = 'n';
            } else if (c == '\r') { dst[di++] = '\\'; dst[di++] = 'r';
            } else if (c == '\t') { dst[di++] = '\\'; dst[di++] = 't';
            } else if (c < 0x20 || c == 0x7f) {
                dst[di++] = '\\'; dst[di++] = 'u';
                dst[di++] = '0';  dst[di++] = '0';
                dst[di++] = hexChar(c >> 4);
                dst[di++] = hexChar(c & 0x0fu);
            } else {
                dst[di++] = static_cast<char>(c);
            }
        }
        dst[di] = '\0';
    }

    static char hexChar(unsigned int n) {
        return static_cast<char>(n < 10u ? '0' + n : 'a' + n - 10u);
    }
};

// Out-of-class definitions for thread_local statics.
// The template parameter ensures PosixLogWriteAdapter and
// ServerLogWriteAdapter each get independent storage.
template <typename D>
thread_local int  JsonLogBase<D>::nestingDepth = 0;

template <typename D>
thread_local bool JsonLogBase<D>::rootArrayOpen = false;

template <typename D>
thread_local int  JsonLogBase<D>::pendingLen = 0;

template <typename D>
thread_local char JsonLogBase<D>::pendingBuf[CAPIO_LOG_MAX_MSG_LEN * 6 + 256] = {'\0'};

#endif // CAPIO_LOGGER_JSON_BASE_HPP