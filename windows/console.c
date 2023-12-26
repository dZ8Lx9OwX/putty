/*
 * console.c - various interactive-prompt routines shared between
 * the Windows console PuTTY tools
 */

#include <stdio.h>
#include <stdlib.h>

#include "putty.h"
#include "storage.h"
#include "ssh.h"
#include "console.h"

void cleanup_exit(int code)
{
    /*
     * Clean up.
     */
    sk_cleanup();

    random_save_seed();

    exit(code);
}

void console_print_error_msg(const char *prefix, const char *msg)
{
    fputs(prefix, stderr);
    fputs(": ", stderr);
    fputs(msg, stderr);
    fputc('\n', stderr);
    fflush(stderr);
}

/*
 * Helper function to print the message from a SeatDialogText. Returns
 * the final prompt to print on the input line, or NULL if a
 * batch-mode abort is needed. In the latter case it will have printed
 * the abort text already.
 */
static const char *console_print_seatdialogtext(SeatDialogText *text)
{
    const char *prompt = NULL;
    stdio_sink errsink[1];
    stdio_sink_init(errsink, stderr);

    for (SeatDialogTextItem *item = text->items,
             *end = item+text->nitems; item < end; item++) {
        switch (item->type) {
          case SDT_PARA:
            wordwrap(BinarySink_UPCAST(errsink),
                     ptrlen_from_asciz(item->text), 60);
            fputc('\n', stderr);
            break;
          case SDT_DISPLAY:
            fprintf(stderr, "  %s\n", item->text);
            break;
          case SDT_SCARY_HEADING:
            /* Can't change font size or weight in this context */
            fprintf(stderr, "%s\n", item->text);
            break;
          case SDT_BATCH_ABORT:
            if (console_batch_mode) {
                fprintf(stderr, "%s\n", item->text);
                fflush(stderr);
                return NULL;
            }
            break;
          case SDT_PROMPT:
            prompt = item->text;
            break;
          default:
            break;
        }
    }
    assert(prompt); /* something in the SeatDialogText should have set this */
    return prompt;
}

SeatPromptResult console_confirm_ssh_host_key(
    Seat *seat, const char *host, int port, const char *keytype,
    char *keystr, SeatDialogText *text, HelpCtx helpctx,
    void (*callback)(void *ctx, SeatPromptResult result), void *ctx)
{
    HANDLE hin;
    DWORD savemode, i;

    const char *prompt = console_print_seatdialogtext(text);
    if (!prompt)
        return SPR_SW_ABORT("Cannot confirm a host key in batch mode");

    char line[32];

    while (true) {
        fprintf(stderr,
                "%s (y/n, 返回取消连接, i 了解更多信息) ",
                prompt);
        fflush(stderr);

        line[0] = '\0';    /* fail safe if ReadFile returns no data */

        hin = GetStdHandle(STD_INPUT_HANDLE);
        GetConsoleMode(hin, &savemode);
        SetConsoleMode(hin, (savemode | ENABLE_ECHO_INPUT |
                             ENABLE_PROCESSED_INPUT | ENABLE_LINE_INPUT));
        ReadFile(hin, line, sizeof(line) - 1, &i, NULL);
        SetConsoleMode(hin, savemode);

        if (line[0] == 'i' || line[0] == 'I') {
            for (SeatDialogTextItem *item = text->items,
                     *end = item+text->nitems; item < end; item++) {
                switch (item->type) {
                  case SDT_MORE_INFO_KEY:
                    fprintf(stderr, "%s", item->text);
                    break;
                  case SDT_MORE_INFO_VALUE_SHORT:
                    fprintf(stderr, ": %s\n", item->text);
                    break;
                  case SDT_MORE_INFO_VALUE_BLOB:
                    fprintf(stderr, ":\n%s\n", item->text);
                    break;
                  default:
                    break;
                }
            }
        } else {
            break;
        }
    }

    /* In case of misplaced reflexes from another program, also recognise 'q'
     * as 'abandon connection rather than trust this key' */
    if (line[0] != '\0' && line[0] != '\r' && line[0] != '\n' &&
        line[0] != 'q' && line[0] != 'Q') {
        if (line[0] == 'y' || line[0] == 'Y')
            store_host_key(host, port, keytype, keystr);
        return SPR_OK;
    } else {
        fputs(console_abandoned_msg, stderr);
        return SPR_USER_ABORT;
    }
}

SeatPromptResult console_confirm_weak_crypto_primitive(
    Seat *seat, SeatDialogText *text,
    void (*callback)(void *ctx, SeatPromptResult result), void *ctx)
{
    HANDLE hin;
    DWORD savemode, i;

    const char *prompt = console_print_seatdialogtext(text);
    if (!prompt)
        return SPR_SW_ABORT("无法在批处理模式下确认"
                            "原始的弱加密");

    char line[32];

    fprintf(stderr, "%s (y/n) ", prompt);
    fflush(stderr);

    hin = GetStdHandle(STD_INPUT_HANDLE);
    GetConsoleMode(hin, &savemode);
    SetConsoleMode(hin, (savemode | ENABLE_ECHO_INPUT |
                         ENABLE_PROCESSED_INPUT | ENABLE_LINE_INPUT));
    ReadFile(hin, line, sizeof(line) - 1, &i, NULL);
    SetConsoleMode(hin, savemode);

    if (line[0] == 'y' || line[0] == 'Y') {
        return SPR_OK;
    } else {
        fputs(console_abandoned_msg, stderr);
        return SPR_USER_ABORT;
    }
}

SeatPromptResult console_confirm_weak_cached_hostkey(
    Seat *seat, SeatDialogText *text,
    void (*callback)(void *ctx, SeatPromptResult result), void *ctx)
{
    HANDLE hin;
    DWORD savemode, i;

    const char *prompt = console_print_seatdialogtext(text);
    if (!prompt)
        return SPR_SW_ABORT("Cannot confirm a weak cached host key "
                            "in batch mode");

    char line[32];

    fprintf(stderr, "%s (y/n) ", prompt);
    fflush(stderr);

    hin = GetStdHandle(STD_INPUT_HANDLE);
    GetConsoleMode(hin, &savemode);
    SetConsoleMode(hin, (savemode | ENABLE_ECHO_INPUT |
                         ENABLE_PROCESSED_INPUT | ENABLE_LINE_INPUT));
    ReadFile(hin, line, sizeof(line) - 1, &i, NULL);
    SetConsoleMode(hin, savemode);

    if (line[0] == 'y' || line[0] == 'Y') {
        return SPR_OK;
    } else {
        fputs(console_abandoned_msg, stderr);
        return SPR_USER_ABORT;
    }
}

bool is_interactive(void)
{
    return is_console_handle(GetStdHandle(STD_INPUT_HANDLE));
}

bool console_antispoof_prompt = true;

void console_set_trust_status(Seat *seat, bool trusted)
{
    /* Do nothing in response to a change of trust status, because
     * there's nothing we can do in a console environment. However,
     * the query function below will make a fiddly decision about
     * whether to tell the backend to enable fallback handling. */
}

bool console_can_set_trust_status(Seat *seat)
{
    if (console_batch_mode) {
        /*
         * In batch mode, we don't need to worry about the server
         * mimicking our interactive authentication, because the user
         * already knows not to expect any.
         */
        return true;
    }

    return false;
}

bool console_has_mixed_input_stream(Seat *seat)
{
    if (!is_interactive() || !console_antispoof_prompt) {
        /*
         * If standard input isn't connected to a terminal, then even
         * if the server did send a spoof authentication prompt, the
         * user couldn't respond to it via the terminal anyway.
         *
         * We also pretend this is true if the user has purposely
         * disabled the antispoof prompt.
         */
        return false;
    }

    return true;
}

/*
 * Ask whether to wipe a session log file before writing to it.
 * Returns 2 for wipe, 1 for append, 0 for cancel (don't log).
 */
int console_askappend(LogPolicy *lp, Filename *filename,
                      void (*callback)(void *ctx, int result), void *ctx)
{
    HANDLE hin;
    DWORD savemode, i;

    static const char msgtemplate[] =
        "会话日志文件\"%.*s\"已存在。\n"
        "您可以用新的会话日志覆盖它，\n"
        "或者将新的会话日志附加到它的末尾，\n"
        "或者禁止此次会话的日志记录。\n"
        "选择“是”擦除文件，“否”追加到末尾，\n"
        "或者“取消”禁用此次日志记录。\n"
        "擦除日志文件吗？(Y/N, Cancel取消日志)";

    static const char msgtemplate_batch[] =
        "会话日志文件\"%。*s\"已经存在。\n"
        "不会启动日志记录。\n";

    char line[32];

    if (console_batch_mode) {
        fprintf(stderr, msgtemplate_batch, FILENAME_MAX, filename->path);
        fflush(stderr);
        return 0;
    }
    fprintf(stderr, msgtemplate, FILENAME_MAX, filename->path);
    fflush(stderr);

    hin = GetStdHandle(STD_INPUT_HANDLE);
    GetConsoleMode(hin, &savemode);
    SetConsoleMode(hin, (savemode | ENABLE_ECHO_INPUT |
                         ENABLE_PROCESSED_INPUT | ENABLE_LINE_INPUT));
    ReadFile(hin, line, sizeof(line) - 1, &i, NULL);
    SetConsoleMode(hin, savemode);

    if (line[0] == 'y' || line[0] == 'Y')
        return 2;
    else if (line[0] == 'n' || line[0] == 'N')
        return 1;
    else
        return 0;
}

/*
 * Warn about the obsolescent key file format.
 *
 * Uniquely among these functions, this one does _not_ expect a
 * frontend handle. This means that if PuTTY is ported to a
 * platform which requires frontend handles, this function will be
 * an anomaly. Fortunately, the problem it addresses will not have
 * been present on that platform, so it can plausibly be
 * implemented as an empty function.
 */
void old_keyfile_warning(void)
{
    static const char message[] =
        "您正在加载一个旧版本的SSH-2私钥文件。\n"
        "这意味着当前密钥文件不是完全防篡改的。\n"
        "未来版本的程序可能会停止支持这种私钥，\n"
        "所有我们建议您将密钥转换为新的格式。\n"
        "\n"
        "将密钥加载到PuTTYgen中，只需要再次保\n"
        "存即可完成转换。";

    fputs(message, stderr);
}

/*
 * Display the fingerprints of the PGP Master Keys to the user.
 */
void pgp_fingerprints(void)
{
    fputs("These are the fingerprints of the PuTTY PGP Master Keys. They can\n"
          "be used to establish a trust path from this executable to another\n"
          "one. See the manual for more information.\n"
          "(Note: these fingerprints have nothing to do with SSH!)\n"
          "\n"
          "PuTTY Master Key as of " PGP_MASTER_KEY_YEAR
          " (" PGP_MASTER_KEY_DETAILS "):\n"
          "  " PGP_MASTER_KEY_FP "\n\n"
          "Previous Master Key (" PGP_PREV_MASTER_KEY_YEAR
          ", " PGP_PREV_MASTER_KEY_DETAILS "):\n"
          "  " PGP_PREV_MASTER_KEY_FP "\n", stdout);
}

void console_logging_error(LogPolicy *lp, const char *string)
{
    /* Ordinary Event Log entries are displayed in the same way as
     * logging errors, but only in verbose mode */
    fprintf(stderr, "%s\n", string);
    fflush(stderr);
}

void console_eventlog(LogPolicy *lp, const char *string)
{
    /* Ordinary Event Log entries are displayed in the same way as
     * logging errors, but only in verbose mode */
    if (lp_verbose(lp))
        console_logging_error(lp, string);
}

StripCtrlChars *console_stripctrl_new(
    Seat *seat, BinarySink *bs_out, SeatInteractionContext sic)
{
    return stripctrl_new(bs_out, false, 0);
}

static void console_write(HANDLE hout, ptrlen data)
{
    DWORD dummy;
    WriteFile(hout, data.ptr, data.len, &dummy, NULL);
}

SeatPromptResult console_get_userpass_input(prompts_t *p)
{
    HANDLE hin = INVALID_HANDLE_VALUE, hout = INVALID_HANDLE_VALUE;
    size_t curr_prompt;

    /*
     * Zero all the results, in case we abort half-way through.
     */
    {
        int i;
        for (i = 0; i < (int)p->n_prompts; i++)
            prompt_set_result(p->prompts[i], "");
    }

    /*
     * The prompts_t might contain a message to be displayed but no
     * actual prompt. More usually, though, it will contain
     * questions that the user needs to answer, in which case we
     * need to ensure that we're able to get the answers.
     */
    if (p->n_prompts) {
        if (console_batch_mode)
            return SPR_SW_ABORT("在批处理模式下无法回应"
                                "交互式提示");
        hin = GetStdHandle(STD_INPUT_HANDLE);
        if (hin == INVALID_HANDLE_VALUE) {
            fprintf(stderr, "Cannot get standard input handle\n");
            cleanup_exit(1);
        }
    }

    /*
     * And if we have anything to print, we need standard output.
     */
    if ((p->name_reqd && p->name) || p->instruction || p->n_prompts) {
        hout = GetStdHandle(STD_OUTPUT_HANDLE);
        if (hout == INVALID_HANDLE_VALUE) {
            fprintf(stderr, "Cannot get standard output handle\n");
            cleanup_exit(1);
        }
    }

    /*
     * Preamble.
     */
    /* We only print the `name' caption if we have to... */
    if (p->name_reqd && p->name) {
        ptrlen plname = ptrlen_from_asciz(p->name);
        console_write(hout, plname);
        if (!ptrlen_endswith(plname, PTRLEN_LITERAL("\n"), NULL))
            console_write(hout, PTRLEN_LITERAL("\n"));
    }
    /* ...but we always print any `instruction'. */
    if (p->instruction) {
        ptrlen plinst = ptrlen_from_asciz(p->instruction);
        console_write(hout, plinst);
        if (!ptrlen_endswith(plinst, PTRLEN_LITERAL("\n"), NULL))
            console_write(hout, PTRLEN_LITERAL("\n"));
    }

    for (curr_prompt = 0; curr_prompt < p->n_prompts; curr_prompt++) {

        DWORD savemode, newmode;
        prompt_t *pr = p->prompts[curr_prompt];

        GetConsoleMode(hin, &savemode);
        newmode = savemode | ENABLE_PROCESSED_INPUT | ENABLE_LINE_INPUT;
        if (!pr->echo)
            newmode &= ~ENABLE_ECHO_INPUT;
        else
            newmode |= ENABLE_ECHO_INPUT;
        SetConsoleMode(hin, newmode);

        console_write(hout, ptrlen_from_asciz(pr->prompt));

        bool failed = false;
        SeatPromptResult spr;
        while (1) {
            /*
             * Amount of data to try to read from the console in one
             * go. This isn't completely arbitrary: a user reported
             * that trying to read more than 31366 bytes at a time
             * would fail with ERROR_NOT_ENOUGH_MEMORY on Windows 7,
             * and Ruby's Win32 support module has evidence of a
             * similar workaround:
             *
             * https://github.com/ruby/ruby/blob/0aa5195262d4193d3accf3e6b9bad236238b816b/win32/win32.c#L6842
             *
             * To keep things simple, I stick with a nice round power
             * of 2 rather than trying to go to the very limit of that
             * bug. (We're typically reading user passphrases and the
             * like here, so even this much is overkill really.)
             */
            DWORD toread = 16384;

            size_t prev_result_len = pr->result->len;
            void *ptr = strbuf_append(pr->result, toread);

            DWORD ret = 0;
            if (!ReadFile(hin, ptr, toread, &ret, NULL)) {
                /* An OS error when reading from the console is treated as an
                 * unexpected error and reported to the user. */
                failed = true;
                spr = make_spr_sw_abort_winerror(
                    "从控制台读取时出错", GetLastError());
                break;
            } else if (ret == 0) {
                /* Regard EOF on the terminal as a deliberate user-abort */
                failed = true;
                spr = SPR_USER_ABORT;
                break;
            }

            strbuf_shrink_to(pr->result, prev_result_len + ret);
            if (strbuf_chomp(pr->result, '\n')) {
                strbuf_chomp(pr->result, '\r');
                break;
            }
        }

        SetConsoleMode(hin, savemode);

        if (!pr->echo)
            console_write(hout, PTRLEN_LITERAL("\r\n"));

        if (failed)
            return spr;
    }

    return SPR_OK;
}
