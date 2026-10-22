/*!
 * @file main.cpp
 * Main for the game. Launches the runtime.
 */

#define STBI_WINDOWS_UTF8

#include <cstdio>
#include <cstring>
#include <string>

#if defined(__ANDROID__) && defined(__aarch64__)
#include <csignal>
#include <fcntl.h>
#include <ucontext.h>
#include <unistd.h>

#include "game/kernel/jak1/kscheme.h"

// <config>/crash.txt in the APK build, opened once at startup; -1 elsewhere. (AI-assisted)
static int g_crash_report_fd = -1;
#endif

#ifdef OPENGOAL_ANDROID_APK_BUILD
// Only defined for the real APK build (game/CMakeLists.txt's `main` shared-library target, OFF by
// default) - NOT the headless scripts/android-port/cmake-poc executable, which is launched
// directly via `adb shell ./gk` and needs a real, undisguised `main()`. SDL3/SDL_main.h
// #define's main to SDL_main when included (see its own comment for the exact condition), which
// is exactly the "SDL_main" symbol SDLActivity.java's getMainFunction() expects to dlsym out of
// libmain.so and call directly - the `int main(int argc, char** argv)` below becomes that
// symbol unchanged, no other code here needs to know about this.
#include <SDL3/SDL_main.h>

#include <jni.h>

#include "common/log/log.h"

// Called by Jak1Activity before it ends the game process from its menu: killProcess skips the
// logger's buffered tail, which a bug report then misses (the last ~15 s). (AI-assisted)
extern "C" JNIEXPORT void JNICALL
Java_com_opengoalandroid_jak1_Jak1Activity_nativeFlushLog(JNIEnv*, jclass) {
  lg::finish();
}

// TEMP DEBUG: unlike the headless scripts/android-port/cmake-poc executable (launched via
// `adb shell`, where stdout/stderr are the actual shell's pipes), a real Activity-launched APK
// has no terminal at all - plain stdout/stderr (what lg::print()/fmt::print() actually write to,
// common/log/log.cpp) silently go nowhere, not even logcat, unless explicitly redirected. Pipes
// both through __android_log_write under a "native-stdout"/"native-stderr" tag so this project's
// existing logging is visible without changing common/log/log.cpp itself. See the development notes.
#include <android/log.h>
#include <unistd.h>

#include <thread>
static void android_redirect_stdio_to_logcat() {
  auto pipe_to_logcat = [](int std_fd, const char* tag, android_LogPriority prio) {
    int pipe_fds[2];
    if (pipe(pipe_fds) != 0) {
      return;
    }
    dup2(pipe_fds[1], std_fd);
    close(pipe_fds[1]);
    int read_fd = pipe_fds[0];
    std::thread([read_fd, tag, prio]() {
      char buf[1024];
      FILE* in = fdopen(read_fd, "r");
      if (!in) {
        return;
      }
      while (fgets(buf, sizeof(buf), in)) {
        size_t len = strlen(buf);
        if (len > 0 && buf[len - 1] == '\n') {
          buf[len - 1] = '\0';
        }
        __android_log_write(prio, tag, buf);
      }
    }).detach();
  };
  setvbuf(stdout, nullptr, _IOLBF, 0);
  setvbuf(stderr, nullptr, _IONBF, 0);
  pipe_to_logcat(STDOUT_FILENO, "native-stdout", ANDROID_LOG_INFO);
  pipe_to_logcat(STDERR_FILENO, "native-stderr", ANDROID_LOG_ERROR);
}
#endif

#include "runtime.h"

#include "common/global_profiler/GlobalProfiler.h"
#include "common/log/log.h"
#include "common/util/FileUtil.h"
#include "common/util/dialogs.h"
#include "common/util/os.h"
#include "common/util/term_util.h"
#include "common/util/unicode_util.h"
#include "common/versions/versions.h"

#include "game/common/game_common_types.h"
#include "graphics/gfx_test.h"

#include "third-party/CLI11.hpp"

#ifdef _WIN32
extern "C" {
__declspec(dllexport) unsigned long NvOptimusEnablement = 0x00000001;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}
#endif

/*!
 * Set up logging system to log to file.
 * @param verbose : should we print debug-level messages to stdout?
 */
void setup_logging(const std::string& game_name, bool verbose, bool disable_ansi_colors) {
  lg::set_file(game_name);
  if (verbose) {
    lg::set_file_level(lg::level::debug);
    lg::set_stdout_level(lg::level::debug);
    lg::set_flush_level(lg::level::debug);
  } else {
    lg::set_file_level(lg::level::debug);
    lg::set_stdout_level(lg::level::info);
    lg::set_flush_level(lg::level::warn);
  }
  if (disable_ansi_colors) {
    lg::disable_ansi_colors();
  }
  lg::initialize();
}

std::string game_arg_documentation() {
  // clang-format off
  std::string output = fmt::format(fmt::emphasis::bold, "Game Args (passed through to the game runtime after '--')\n");
  output += fmt::format(fmt::fg(fmt::color::gray), "Order matters, some args will negate others (see kmachine.cpp for details)\n");
  output += fmt::format(fmt::fg(fmt::color::gray), "Args with `*` are not well supported\n\n");
  // Common args
  output += fmt::format(fmt::emphasis::bold, "Common:\n");
  output += "  -cd          * Use the DVD drive for everything. This is how the game runs in retail\n";
  output += "  -cddata      * Use the DVD drive for everything but IOP modules\n";
  output += "  -deviso      * One of two modes for testing without the need for DVDs\n";
  output += "  -fakeiso       The other of two modes for testing without the need for DVDs\n";
  output += "  -boot          Used to set GOAL up for running the game in retail mode\n";
  output += "  -debug         Used to set GOAL up for debugging/development\n";
  output += "  -debug-mem     Used to set up GOAL in debug mode, but not to load debug-segments\n";
  output += "  -nokernel      An added mode to allow booting without a KERNEL.CGO for testing\n";
  output += "  -nosound       An added mode to allow booting without sound for testing\n";
  output += "  -level [name]  Used to inform the game to boot a specific level the default level is `#f`\n";
  // Jak 1 Related
  output += fmt::format(fmt::emphasis::bold | fmt::fg(fmt::color::orange), "Jak 1:\n");
  output += "  -demo          Boot the game in demo mode\n";
  // Jak 2 only
  output += fmt::format(fmt::emphasis::bold | fmt::fg(fmt::color::purple), "Jak 2:\n");
  output += "  -demo          Boot the game in demo mode\n";
  output += "  -kiosk         Boot the game in kiosk demo mode\n";
  output += "  -preview       Boot the game in preview demo mode\n";
  output += "  -debug-boot    Used to boot the game in retail mode, but with debug segments\n";
  output += "  -user [name]   Specify the debugging username, the default is `unknown`\n";
  output += "  -art [name]    Specify the art-group name to set `DebugBootArtGroup`, there is no default\n";
  // clang-format on
  return output;
}

/*!
 * Entry point for the game.
 */
int main(int argc, char** argv) {
#ifdef OPENGOAL_ANDROID_APK_BUILD
  android_redirect_stdio_to_logcat();
#endif
  ArgumentGuard u8_guard(argc, argv);

#if defined(__ANDROID__) && defined(__aarch64__)
  // TEMP DEBUG: dump the faulting PC/registers on SIGILL/SIGSEGV so we can find exactly which
  // instruction the ARM64 JIT backend actually crashes on, instead of guessing from static
  // disassembly. See the development notes.
  static struct sigaction old_sigill_action;
  static struct sigaction old_sigsegv_action;
  static struct sigaction old_sigbus_action;
  static struct sigaction old_sigabrt_action;
  auto crash_handler = [](int sig, siginfo_t* info, void* ucontext_raw) {
    auto* uc = (ucontext_t*)ucontext_raw;
    // In the APK, send the dump to <config>/crash.txt (opened at startup) instead of logcat so the
    // launcher can attach it to a bug report. dup2 is async-signal-safe. (AI-assisted)
    if (g_crash_report_fd >= 0) {
      dup2(g_crash_report_fd, STDERR_FILENO);
    }
    fprintf(stderr, "\n=== TEMP DEBUG CRASH HANDLER ===\n");
    fprintf(stderr, "signal: %d (%s)\n", sig,
            sig == SIGILL    ? "SIGILL"
            : sig == SIGBUS  ? "SIGBUS"
            : sig == SIGABRT ? "SIGABRT (failed assertion, see the game log)"
                             : "SIGSEGV");
    fprintf(stderr, "fault addr (siginfo->si_addr): %p\n", info->si_addr);
    fprintf(stderr, "pc: 0x%llx\n", (unsigned long long)uc->uc_mcontext.pc);
    for (int i = 0; i < 31; i++) {
      fprintf(stderr, "x%d: 0x%llx\n", i, (unsigned long long)uc->uc_mcontext.regs[i]);
    }
    fprintf(stderr, "sp: 0x%llx\n", (unsigned long long)uc->uc_mcontext.sp);
    // dump 16 bytes of raw code around the PC
    //
    // Deliberately done *before* symbolication below (reordered this session): `pc` and `lr` are
    // themselves always inside already-executed/about-to-execute JIT'd code, so reading a small
    // window around them can't hit the heap's `PROT_NONE`-protected low region or unmapped memory
    // the way a *data* pointer like `x16` can - safe to do unconditionally, unlike the x16 dump
    // further below. Confirmed necessary this session: `debug_symbolicate_goal_addr()`'s own
    // symbol-table/method-table walk can itself fault (observed once, deep into boot with 1000+
    // objects loaded - some type/method entry not yet fully linked) and silently kill the whole
    // process with zero further output when it was the first thing run, taking the pc/lr byte
    // dumps down with it even though those never needed the symbol table at all. See
    // the development notes.
    // TEMP DEBUG: dump mips2c call-tracking globals (see asm_funcs_arm64.s's _mips2c_call_arm64) -
    // safe to read unconditionally, same reasoning as the pc/lr dump below (plain integers, no
    // pointer dereference).
    extern volatile u64 g_mips2c_debug_last_exec, g_mips2c_debug_last_ret, g_mips2c_debug_call_count;
    fprintf(stderr, "mips2c: call_count=%llu last_exec=0x%llx last_ret=0x%llx\n",
            (unsigned long long)g_mips2c_debug_call_count,
            (unsigned long long)g_mips2c_debug_last_exec,
            (unsigned long long)g_mips2c_debug_last_ret);
    auto* pc_bytes = (unsigned char*)uc->uc_mcontext.pc;
    fprintf(stderr, "bytes at pc: ");
    for (int i = -8; i < 8; i++) {
      fprintf(stderr, "%02x ", pc_bytes[i]);
    }
    fprintf(stderr, "\n");
    // dump bytes around x30 (link register / return address) too - tells us exactly which
    // caller (which asm_funcs_arm64.s trampoline, or which JIT'd GOAL function) made the blr
    // that landed us here, without needing a full ARM64 disassembler attached.
    auto* lr_bytes = (unsigned char*)uc->uc_mcontext.regs[30];
    fprintf(stderr, "bytes at x30 (lr): ");
    for (int i = -64; i < 16; i++) {
      fprintf(stderr, "%02x ", lr_bytes[i]);
    }
    fprintf(stderr, "\n");
    // Wider window forward from the crash PC (the computed call target) - is this a genuinely
    // unwritten/zeroed region (supports "called before the real code was linked in"), or does
    // real-looking code start a few bytes off (supports "wrong offset computed for the call
    // target" - an off-by-N bug in this session's ARM64 link/relocation code, same family as the
    // three already found and fixed this session)?
    fprintf(stderr, "512 bytes forward from pc:\n");
    for (int row = 0; row < 32; row++) {
      fprintf(stderr, "  +0x%03x: ", row * 16);
      for (int col = 0; col < 16; col++) {
        fprintf(stderr, "%02x ", pc_bytes[row * 16 + col]);
      }
      fprintf(stderr, "\n");
    }
    // TEMP DEBUG: also dump backward from pc - the forward-only dump above can't show what
    // actually *computed* a bad address/register value used at the crash site (e.g. a bad `x16`),
    // only what the crashing code was about to do with it. 1024 bytes back, enough to reach a
    // typical function's own prologue (including this session's own X30 push, if this function
    // has one) for context.
    fprintf(stderr, "1024 bytes backward from pc:\n");
    for (int row = -64; row < 0; row++) {
      fprintf(stderr, "  %+05d: ", row * 16);
      for (int col = 0; col < 16; col++) {
        fprintf(stderr, "%02x ", pc_bytes[row * 16 + col]);
      }
      fprintf(stderr, "\n");
    }
    fflush(stderr);
    // Resolve the crashing PC (and the faulting address, if different) back to a GOAL function or
    // method name by scanning the runtime's own symbol table / type method tables - no external
    // debugger or .so symbol table needed, since this is JIT'd GOAL code. See the development notes.
    // Now runs *after* the pc/lr-relative byte dumps above (reordered this session) precisely
    // because this walk isn't as safe as those - see the big comment above `pc_bytes` for why.
    fprintf(stderr, "symbolicating pc:\n");
    if (g_game_version == GameVersion::Jak1) {
      jak1::debug_symbolicate_goal_addr(uc->uc_mcontext.pc);
    }
    if ((u64)info->si_addr != uc->uc_mcontext.pc) {
      fprintf(stderr, "symbolicating fault addr:\n");
      if (g_game_version == GameVersion::Jak1) {
        jak1::debug_symbolicate_goal_addr((u64)info->si_addr);
      }
    }
    fflush(stderr);
    // x16 (already in the register dump above) is the absolute object/base pointer the crashing
    // `ldur w3, [x16, #0x10]` read the (zeroed) call target from - dump around it too, to see
    // whether *this* looks like a real, populated GOAL object (a type tag word followed by
    // plausible-looking data) or is itself in an unwritten region.
    u64 x16_raw = uc->uc_mcontext.regs[16];
    // Guard against x16 itself pointing into the heap's protected low region (or anywhere clearly
    // not backed by the heap mapping at all) - see the big comment above for why an unconditional
    // read here silently kills the whole process instead of erroring.
    bool x16_in_protected_region =
        g_ee_main_mem != nullptr && x16_raw >= (u64)g_ee_main_mem &&
        x16_raw < (u64)g_ee_main_mem + EE_MAIN_MEM_LOW_PROTECT;
    bool x16_outside_heap =
        g_ee_main_mem == nullptr || x16_raw < (u64)g_ee_main_mem + 16 ||
        x16_raw >= (u64)g_ee_main_mem + EE_MAIN_MEM_SIZE - 48;
    if (x16_in_protected_region) {
      fprintf(stderr,
              "x16 (0x%llx) is inside the heap's own PROT_NONE-protected low %dKB region - "
              "skipping the dump around it, reading it would fault a second time\n",
              (unsigned long long)x16_raw, EE_MAIN_MEM_LOW_PROTECT / 1024);
    } else if (x16_outside_heap) {
      fprintf(stderr, "x16 (0x%llx) is outside the mapped heap entirely - skipping the dump\n",
              (unsigned long long)x16_raw);
    } else {
      auto* x16_bytes = (unsigned char*)x16_raw;
      fprintf(stderr, "64 bytes around x16 (object base for the crashing load):\n");
      for (int i = -16; i < 48; i += 16) {
        fprintf(stderr, "  %+04d: ", i);
        for (int col = 0; col < 16; col++) {
          fprintf(stderr, "%02x ", x16_bytes[i + col]);
        }
        fprintf(stderr, "\n");
      }
    }
    fprintf(stderr, "=== END TEMP DEBUG CRASH HANDLER ===\n");
    fflush(stderr);
    // re-raise with default handler so we still get a normal crash/exit
    struct sigaction* old_action =
        (sig == SIGILL)    ? &old_sigill_action
        : (sig == SIGBUS)  ? &old_sigbus_action
        : (sig == SIGABRT) ? &old_sigabrt_action
                           : &old_sigsegv_action;
    sigaction(sig, old_action, nullptr);
    raise(sig);
  };
  // Run the handler on a dedicated alternate stack - if the crash itself is stack corruption/
  // misalignment (exactly the kind of bug this handler exists to catch), the kernel may not be
  // able to deliver the signal on the current (broken) stack at all, silently falling back to the
  // default action with no handler output (observed once before this was added).
  static char altstack[SIGSTKSZ * 4];
  stack_t ss;
  ss.ss_sp = altstack;
  ss.ss_size = sizeof(altstack);
  ss.ss_flags = 0;
  sigaltstack(&ss, nullptr);

  struct sigaction sa;
  memset(&sa, 0, sizeof(sa));
  sa.sa_sigaction = crash_handler;
  sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
  sigemptyset(&sa.sa_mask);
  sigaction(SIGILL, &sa, &old_sigill_action);
  sigaction(SIGSEGV, &sa, &old_sigsegv_action);
  sigaction(SIGBUS, &sa, &old_sigbus_action);
  // ASSERT() ends in abort(): record those too, so the launcher sees them as crashes. (AI-assisted)
  sigaction(SIGABRT, &sa, &old_sigabrt_action);

  // TEMP DEBUG: a second, *non-fatal* handler for SIGUSR2, to sample the PC/registers of a thread
  // that's spinning (not crashing) - e.g. `adb shell run-as <pkg> kill -SIGUSR2 <tid>` at the "EE"
  // thread's tid while it's stuck at ~100% CPU with no further log output, to find exactly which
  // instruction an infinite loop is stuck at without a real debugger attached. See
  // the development notes' "second distinct hang bug" for why this was needed. SIGUSR2 (not SIGUSR1)
  // specifically - confirmed via /proc/<pid>/task/<tid>/status's SigBlk that Android's runtime
  // blocks SIGUSR1 process-wide (likely reserved for ART's own heap-dump trigger), which silently
  // ate every signal sent to it with no handler output and no crash. Does NOT re-raise - the
  // thread resumes right where it was after dumping, so this can be sent repeatedly to watch the
  // PC move (or not move, confirming a tight loop vs. legitimately slow work).
  auto sample_handler = [](int /*sig*/, siginfo_t* /*info*/, void* ucontext_raw) {
    auto* uc = (ucontext_t*)ucontext_raw;
    fprintf(stderr, "\n=== TEMP DEBUG SIGUSR2 SAMPLE ===\n");
    fprintf(stderr, "pc: 0x%llx\n", (unsigned long long)uc->uc_mcontext.pc);
    fprintf(stderr, "lr (x30): 0x%llx\n", (unsigned long long)uc->uc_mcontext.regs[30]);
    for (int i = 0; i < 31; i++) {
      fprintf(stderr, "x%d: 0x%llx\n", i, (unsigned long long)uc->uc_mcontext.regs[i]);
    }
    fprintf(stderr, "sp: 0x%llx\n", (unsigned long long)uc->uc_mcontext.sp);
    fprintf(stderr, "symbolicating pc:\n");
    // Jak 2 has a different symbol-table layout; never walk it as Jak 1. (AI-assisted)
    if (g_game_version == GameVersion::Jak1) {
      jak1::debug_symbolicate_goal_addr(uc->uc_mcontext.pc);
    }
    auto* pc_bytes = (unsigned char*)uc->uc_mcontext.pc;
    fprintf(stderr, "1024 bytes around pc:\n");
    for (int row = -32; row < 32; row++) {
      fprintf(stderr, "  %+05d: ", row * 16);
      for (int col = 0; col < 16; col++) {
        fprintf(stderr, "%02x ", pc_bytes[row * 16 + col]);
      }
      fprintf(stderr, "\n");
    }
    auto* lr_bytes = (unsigned char*)uc->uc_mcontext.regs[30];
    fprintf(stderr, "512 bytes around lr:\n");
    for (int row = -16; row < 16; row++) {
      fprintf(stderr, "  %+05d: ", row * 16);
      for (int col = 0; col < 16; col++) {
        fprintf(stderr, "%02x ", lr_bytes[row * 16 + col]);
      }
      fprintf(stderr, "\n");
    }
    fprintf(stderr, "=== END TEMP DEBUG SIGUSR2 SAMPLE ===\n");
    fflush(stderr);
  };
  struct sigaction sa_sample;
  memset(&sa_sample, 0, sizeof(sa_sample));
  sa_sample.sa_sigaction = sample_handler;
  sa_sample.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_RESTART;
  sigemptyset(&sa_sample.sa_mask);
  sigaction(SIGUSR2, &sa_sample, nullptr);
#endif

#ifdef __SWITCH__
  // Temporary, no-dependencies boot tracer: writes straight to a file on the SD card (which is a
  // real, directly-readable folder on the host PC when running under Ryujinx) so early-exit
  // points can be told apart without needing nxlink or a working lg::/logging setup - both of
  // which depend on code further down that hasn't run yet at the point this is needed. Remove
  // once the game reliably gets past its early boot sequence on Switch.
  FILE* switch_boot_log_fp = fopen("sdmc:/switch/gk_boot.log", "w");
  auto switch_boot_log = [](FILE* fp, const char* msg) {
    if (fp) {
      fprintf(fp, "%s\n", msg);
      fflush(fp);
    }
  };
  switch_boot_log(switch_boot_log_fp, "main() entered");
#endif

#ifdef __SWITCH__
  // nx-hbloader only supplies a real argv (via libnx's crt0) when a homebrew is launched with
  // argument-passing configured (e.g. `nxlink -s`) - a plain launch from the Homebrew Menu's own
  // UI gives argc == 0. CLI11's App::parse(int, const char* const*) unconditionally computes
  // `static_cast<size_t>(argc) - 1U` and loops an index down from that - with argc == 0 that
  // underflows to SIZE_MAX and the loop reads argv[SIZE_MAX], argv[SIZE_MAX - 1], ... as if they
  // were valid pointers, eventually treating whatever garbage bytes it finds as a `const char*`
  // and constructing a std::string from it. This is confirmed (not guessed) as the crash a
  // hardware/Ryujinx test hit: PC in strlen(), called from std::string::assign(const char*),
  // called from CLI::App::parse(), called from main() - found via addr2line/objdump against a
  // matching build's gk.elf. Guard against it with a safe fallback argv instead of trusting the
  // loader gave us one; downstream code (CLI11, set_switch_executable_path() below, etc.) can
  // then keep assuming the normal C convention of argc >= 1.
  static char switch_argv0[] = "gk";
  static char* switch_safe_argv[] = {switch_argv0, nullptr};
  bool had_no_argv = argc <= 0;
  if (had_no_argv) {
    argc = 1;
    argv = switch_safe_argv;
  }

  // libnx's crt0 parses the loader-provided argv (nx-hbloader sets argv[0] to this .nro's
  // on-disk path, e.g. "sdmc:/switch/whatever.nro") before main() runs - stash it now, before
  // anything (setup_project_path(), just below) needs get_current_executable_path(). When argv
  // wasn't provided (the argc<=0 case above), argv[0] is just the placeholder "gk" string with no
  // path in it at all - passing that straight through would make get_current_executable_path()
  // (and therefore the "look for a data/ folder next to the executable" logic in
  // file_util::try_get_data_dir()) resolve to a bogus empty parent directory instead of falling
  // back to a sane guess. Use the same "sdmc:/switch/gk.nro" guess FileUtil.cpp's own
  // get_current_executable_path() fallback already uses for this case.
  file_util::set_switch_executable_path(had_no_argv ? "sdmc:/switch/gk.nro" : argv[0]);
  switch_boot_log(switch_boot_log_fp, "argv/executable path resolved");
#endif

  // CLI flags
  bool show_version = false;
  std::string game_name = "jak1";
  bool verbose_logging = false;
  bool disable_avx2 = false;
  bool disable_display = false;
  bool enable_profiling = false;
  bool enable_portable = false;
  bool disable_save_location_override = false;
  std::string profile_until_event = "";
  std::string gpu_test = "";
  std::string gpu_test_out_path = "";
  int port_number = -1;
  fs::path project_path_override;
  fs::path user_config_dir_override;
  std::vector<std::string> game_args;
  CLI::App app{"OpenGOAL Game Runtime"};
  app.add_flag("--version", show_version, "Display the built revision");
  app.add_option("-g,--game", game_name, "The game name: 'jak1' or 'jak2'");
  app.add_flag("-v,--verbose", verbose_logging, "Enable verbose logging on stdout");
  app.add_flag(
      "--port", port_number,
      "Specify port number for listener connection (default is 8112 for Jak 1 and 8113 for Jak 2)");
  app.add_flag("--no-avx2", disable_avx2, "Disable AVX2 for testing");
  app.add_flag("--no-display", disable_display, "Disable video display");
  app.add_flag("--profile", enable_profiling, "Enables profiling immediately from startup");
  app.add_flag("--portable", enable_portable,
               "Save settings and saves relative to the game's executable, takes precedence over "
               "--config-path");
  app.add_flag("--disable_save_location_override", disable_save_location_override,
               "If --config-path is provided along with this flag, saves will still be loaded and "
               "stored to the default location");
  app.add_option("--profile-until-event", profile_until_event,
                 "Stops recording profile events once an event with this name is seen");
  app.add_option("--gpu-test", gpu_test,
                 "Tests for minimum graphics requirements.  Valid Options are: [opengl]");
  app.add_option("--gpu-test-out-path", gpu_test_out_path,
                 "Where to store the gpu test result file");
  app.add_option("--proj-path", project_path_override,
                 "Specify the location of the 'data/' folder");
  app.add_option("--config-path", user_config_dir_override,
                 "Override the location where all user configuration and saves are saved");
  app.footer(game_arg_documentation());
  app.add_option("Game Args", game_args,
                 "Remaining arguments (after '--') that are passed-through to the game itself");
  define_common_cli_arguments(app);
  app.allow_extras();
  CLI11_PARSE(app, argc, argv);
#ifdef __SWITCH__
  switch_boot_log(switch_boot_log_fp, "CLI11 parse done");
#endif

  // Log the version the game is compiled against so we don't have to guess
  lg::info("Compiled Version: {}", build_revision());

  // Override the user's config dir, potentially (either because it was explicitly provided
  // or because it's portable mode)
  if (enable_portable) {
    lg::info("Portable mod enabled");
    user_config_dir_override = fs::path(file_util::get_current_executable_path()).parent_path();
  }
  if (!user_config_dir_override.empty()) {
    lg::info("Overriding config directory with: {}", user_config_dir_override.string());
    file_util::override_user_config_dir(user_config_dir_override, !disable_save_location_override);
#if defined(OPENGOAL_ANDROID_APK_BUILD) && defined(__aarch64__)
    // Truncated each launch: the launcher copies a previous crash into its report before it
    // starts the game again. (AI-assisted)
    g_crash_report_fd = open((user_config_dir_override / "crash.txt").string().c_str(),
                             O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
#endif
  }

  if (show_version) {
    lg::print("{}", build_revision());
    return 0;
  }

  if (!gpu_test.empty() && !gpu_test_out_path.empty()) {
    const auto output = tests::run_gpu_test(gpu_test);
    json data = output;
    try {
      file_util::write_text_file(gpu_test_out_path, data.dump(2));
    } catch (std::exception& e) {
      return 1;
    }
    return 0;
  }

  prof().set_enable(enable_profiling);
  prof().set_waiting_for_event(profile_until_event);

  // Create struct with all non-kmachine handled args to pass to the runtime
  GameLaunchOptions game_options;
  game_options.disable_display = disable_display;
  game_options.game_version = game_name_to_version(game_name);
  game_options.server_port =
      port_number == -1 ? DECI2_PORT - 1 + (int)game_options.game_version : port_number;

#ifdef __SWITCH__
  switch_boot_log(switch_boot_log_fp, "reached AVX check");
#endif
  // Figure out if the CPU has AVX2 to enable higher performance AVX2 versions of functions.
  setup_cpu_info();
// If the CPU doesn't have AVX, GOAL code won't work and we exit.
// AVX is an x86 SIMD extension - the concept doesn't apply on any ARM64 target (Switch, Android,
// real native Apple Silicon), where `__cpuidex()` is a stub that always reports no AVX/AVX2
// support (see common/util/os.cpp). Skip this whole gate there; goalc's ARM64 JIT backend emits
// native ARM64 instructions, not AVX, so there's nothing to check for.
#ifndef __aarch64__
  if (!get_cpu_info().has_avx) {
// Check if we are on a modern enough version of macOS so that AVX can be
// emulated via rosetta
#ifdef __APPLE__
    auto macos_major_version = get_macos_major_version();
    if (macos_major_version < 15.0) {
      lg::info(
          "Your CPU does not support AVX. But the newer version of Rosetta supports it, update to "
          "atleast Sequoia to run OpenGOAL!");
      dialogs::create_error_message_dialog(
          "Unmet Requirements",
          "Your CPU does not support AVX. But the newer version of Rosetta supports it, update to "
          "atleast Sequoia to run OpenGOAL!");
      return -1;
    }
#else
    lg::info("Your CPU does not support AVX, which is required for OpenGOAL.");
    dialogs::create_error_message_dialog(
        "Unmet Requirements", "Your CPU does not support AVX, which is required for OpenGOAL.");
    return -1;
#endif
  }
#endif

#ifdef __SWITCH__
  switch_boot_log(switch_boot_log_fp, "passed AVX check, entering setup_project_path");
#endif
  // set up file paths for resources. This is the full repository when developing, and the data
  // directory (a subset of the full repo) in release versions
  if (project_path_override.empty()) {
    lg::info("No project path provided, looking for data/ folder in current directory");
    if (!file_util::setup_project_path({})) {
#ifdef __SWITCH__
      switch_boot_log(switch_boot_log_fp, "setup_project_path({}) FAILED - returning 1");
#endif
      return 1;
    }
  } else if (!file_util::setup_project_path(project_path_override)) {
#ifdef __SWITCH__
    switch_boot_log(switch_boot_log_fp, "setup_project_path(override) FAILED - returning 1");
#endif
    return 1;
  }
#ifdef __SWITCH__
  switch_boot_log(switch_boot_log_fp, "setup_project_path OK");
#endif

  if (disable_avx2) {
    // for debugging the non-avx2 code paths, there's a flag to manually disable.
    lg::info("Note: AVX2 code has been manually disabled.");
    get_cpu_info().has_avx2 = false;
  }

#ifndef __AVX2__
  if (get_cpu_info().has_avx2) {
    // printf("Note: your CPU supports AVX2, but this build was not compiled with AVX2 support\n");
    get_cpu_info().has_avx2 = false;
  }
#endif

  if (get_cpu_info().has_avx2) {
    lg::info("AVX2 mode enabled");
  } else {
    lg::info("AVX2 mode disabled");
  }

#ifdef __SWITCH__
  switch_boot_log(switch_boot_log_fp, "entering setup_logging");
#endif
  try {
    setup_logging(game_name, verbose_logging, _cli_flag_disable_ansi);
  } catch (const std::exception& e) {
#ifdef __SWITCH__
    switch_boot_log(switch_boot_log_fp, "setup_logging THREW - returning 1");
#endif
    lg::error("Failed to setup logging: {}", e.what());
    return 1;
  }
#ifdef __SWITCH__
  switch_boot_log(switch_boot_log_fp, "setup_logging OK, entering runtime loop");
#endif

  bool force_debug_next_time = false;
  // always start with an empty arg, as internally kmachine starts at `1` not `0`
  std::vector<const char*> arg_ptrs = {""};
  for (auto& str : game_args) {
    arg_ptrs.push_back(str.data());
  }

  while (true) {
    if (force_debug_next_time) {
      // I'd like to check and not add duplicates, unfortunately since the game
      // cares about ordering...that's likely error prone if the user passed args in the wrong order
      // ie. -debug -boot (we'd skip adding things, but the order would be wrong).
      game_args.push_back("-boot");
      game_args.push_back("-debug");
      force_debug_next_time = false;
      arg_ptrs = {""};  // see above for rationale
      for (auto& str : game_args) {
        arg_ptrs.push_back(str.data());
      }
    }

    // run the runtime in a loop so we can reset the game and have it restart cleanly
    lg::info("OpenGOAL Runtime {}.{}", versions::GOAL_VERSION_MAJOR, versions::GOAL_VERSION_MINOR);
#ifdef __SWITCH__
    switch_boot_log(switch_boot_log_fp, "calling exec_runtime");
#endif
    try {
      MasterExit = RuntimeExitStatus::RUNNING;
      auto exit_status = exec_runtime(game_options, arg_ptrs.size(), arg_ptrs.data());
#ifdef __SWITCH__
      switch_boot_log(switch_boot_log_fp, "exec_runtime returned");
#endif
      switch (exit_status) {
        case RuntimeExitStatus::EXIT:
          return 0;
        case RuntimeExitStatus::RESTART_RUNTIME:
        case RuntimeExitStatus::RUNNING:
          break;
        case RuntimeExitStatus::RESTART_IN_DEBUG:
          force_debug_next_time = true;
          break;
      }
    } catch (std::exception& ex) {
#ifdef __SWITCH__
      switch_boot_log(switch_boot_log_fp, "exec_runtime THREW an exception");
#endif
      lg::error("Unexpected exception occurred - {}", ex.what());
      throw ex;
    }
  }
  return 0;
}
