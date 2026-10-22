#include <cstdio>
#include <regex>

#include "common/log/log.h"
#include "common/repl/nrepl/ReplServer.h"
#include "common/repl/repl_wrapper.h"
#include "common/util/FileUtil.h"
#include "common/util/diff.h"
#include "common/util/font/font_utils_korean.h"
#include "common/util/string_util.h"
#include "common/util/term_util.h"
#include "common/util/unicode_util.h"
#include "common/versions/versions.h"

#include "goalc/compiler/Compiler.h"
#include "goalc/emitter/InstructionSet.h"

#include "fmt/color.h"
#include "fmt/format.h"
#include "third-party/CLI11.hpp"

void setup_logging(const bool disable_ansi_colors) {
  lg::set_file_level(lg::level::info);
  lg::set_stdout_level(lg::level::info);
  lg::set_flush_level(lg::level::info);
  if (disable_ansi_colors) {
    lg::disable_ansi_colors();
  }
  lg::set_file("compiler");
  lg::initialize();
}

int main(int argc, char** argv) {
  ArgumentGuard u8_guard(argc, argv);

  bool auto_find_user = false;
  std::string cmd = "";
  std::string username = "#f";
  std::string game = "jak1";
  int nrepl_port = -1;
  fs::path project_path_override;
  fs::path iso_path_override;

  // TODO - a lot of these flags could be deprecated and moved into `repl-config.json`
  CLI::App app{"OpenGOAL Compiler / REPL"};
  app.add_option("-c,--cmd", cmd, "Specify a command to run, no REPL is launched in this mode");
  std::string script_file;
  app.add_option("--script", script_file,
                 "Run each top-level form of FILE as if typed into the REPL (so code is sent to the "
                 "target once (lt) connects), then exit without resetting the target. "
                 "(script-wait SECONDS) pauses between forms. (AI-assisted)");
  app.add_option("-u,--user", username,
                 "Specify the username to use for your user profile in 'goal_src/user/'");
  app.add_option("-p,--port", nrepl_port, "Specify the nREPL port.  Defaults to 8181");
  app.add_flag("--user-auto", auto_find_user,
               "Attempt to automatically deduce the user, overrides '--user'");
  app.add_option("-g,--game", game, "The game name: 'jak1' or 'jak2'");
  app.add_option("--proj-path", project_path_override,
                 "Specify the location of the 'data/' folder");
  app.add_option("--iso-path", iso_path_override, "Specify the location of the 'iso_data/' folder");
  std::string arch = "x86";
  app.add_option("-a,--arch", arch,
                 "Target instruction set for emitted code: 'x86' (default) or 'arm64'. The "
                 "ARM64 backend is a work-in-progress upstream/research effort (see "
                 "the development notes) - selecting it does not imply compiled code has been "
                 "verified correct, only that this build attempts to emit it.");
  define_common_cli_arguments(app);
  app.validate_positionals();
  CLI11_PARSE(app, argc, argv);

  emitter::InstructionSet instruction_set;
  if (arch == "x86") {
    instruction_set = emitter::InstructionSet::X86;
  } else if (arch == "arm64") {
    instruction_set = emitter::InstructionSet::ARM64;
  } else {
    lg::error("Error: unknown --arch '{}', expected 'x86' or 'arm64'", arch);
    return 1;
  }

  GameVersion game_version = game_name_to_version(game);

  if (!project_path_override.empty()) {
    if (!fs::exists(project_path_override)) {
      lg::error("Error: project path override '{}' does not exist", project_path_override.string());
      return 1;
    }
    if (!file_util::setup_project_path(project_path_override, true)) {
      lg::error("Could not setup project path!");
      return 1;
    }
  } else if (!file_util::setup_project_path(std::nullopt, true)) {
    return 1;
  }

  try {
    setup_logging(_cli_flag_disable_ansi);
  } catch (const std::exception& e) {
    lg::error("Failed to setup logging: {}", e.what());
    return 1;
  }

  // Figure out the username
  if (auto_find_user) {
    username = REPL::find_repl_username();
  }
  // Load the user's startup file
  auto startup_file = REPL::load_user_startup_file(username, game_version);
  // Load the user's REPL config
  auto repl_config = REPL::load_repl_config(username, game_version, nrepl_port);

  // Check for a custom ISO path before we instantiate the compiler.
  if (!iso_path_override.empty()) {
    if (!fs::exists(iso_path_override)) {
      lg::error("Error: iso path override '{}' does not exist", iso_path_override.string());
      return 1;
    }
    file_util::set_iso_data_dir(iso_path_override);
    repl_config.iso_path = iso_path_override.string();
  }

  // Init Compiler
  std::unique_ptr<Compiler> compiler;
  std::mutex compiler_mutex;
  // if a command is provided on the command line, no REPL just run the compiler on it
  try {
    if (!cmd.empty()) {
      compiler = std::make_unique<Compiler>(game_version, instruction_set);
      compiler->run_front_end_on_string(cmd);
      return 0;
    }
  } catch (std::exception& e) {
    lg::error("Compiler Fatal Error: {}", e.what());
    return 1;
  }

  // Scripted REPL session, used by scripts/android-port/devloop.sh to drive a running game.
  // (AI-assisted)
  if (!script_file.empty()) {
    try {
      compiler = std::make_unique<Compiler>(
          game_version, instruction_set, std::make_optional(repl_config), username,
          std::make_unique<REPL::Wrapper>(username, repl_config, startup_file, false));
      goos::Reader reader;
      auto forms = reader.read_from_string(file_util::read_text_file(fs::path(script_file)));
      // read_from_string wraps everything in (top-level ...)
      auto* it = &forms.as_pair()->cdr;
      while (it->is_pair()) {
        const auto& form = it->as_pair()->car;
        if (form.is_pair() && form.as_pair()->car.is_symbol() &&
            form.as_pair()->car.as_symbol().name_ptr == std::string("script-wait")) {
          const auto& arg = form.as_pair()->cdr.as_pair()->car;
          double seconds = arg.is_float() ? arg.as_float() : (double)arg.as_int();
          lg::info("[script] wait {}s", seconds);
          std::this_thread::sleep_for(std::chrono::milliseconds((int64_t)(seconds * 1000)));
        } else {
          lg::info("[script] > {}", form.print());
          if (compiler->handle_repl_string(form.print()) == ReplStatus::WANT_EXIT) {
            break;
          }
        }
        it = &it->as_pair()->cdr;
      }
      // Leave the game running: the Compiler destructor would otherwise reset the target.
      if (compiler->listener().is_connected()) {
        compiler->listener().disconnect();
      }
    } catch (std::exception& e) {
      lg::error("Script Error: {}", e.what());
      return 1;
    }
    return 0;
  }

  // Otherwise, start the REPL normally
  ReplStatus status = ReplStatus::OK;
  std::function<void()> repl_startup_func = [&]() {
    // Run automatic forms if applicable
    std::lock_guard<std::mutex> lock(compiler_mutex);
    for (const auto& cmd : startup_file.run_before_listen) {
      status = compiler->handle_repl_string(cmd);
    }
  };

  // Initialize nREPL server socket
  std::function<bool()> shutdown_callback = [&]() { return status == ReplStatus::WANT_EXIT; };
  ReplServer repl_server(shutdown_callback, repl_config.get_nrepl_port());
  bool nrepl_server_ok = repl_server.init_server(true);
  std::thread nrepl_thread;
  // the compiler may throw an exception if it fails to load its standard library.
  try {
    compiler = std::make_unique<Compiler>(
        game_version, instruction_set, std::make_optional(repl_config), username,
        std::make_unique<REPL::Wrapper>(username, repl_config, startup_file, nrepl_server_ok));
    // Start nREPL Server if it spun up successfully
    if (nrepl_server_ok) {
      nrepl_thread = std::thread([&]() {
        while (!shutdown_callback()) {
          auto resp = repl_server.get_msg();
          if (resp) {
            std::lock_guard<std::mutex> lock(compiler_mutex);
            status = compiler->handle_repl_string(resp.value());
            // Print out the prompt, just for better UX
            compiler->print_to_repl(compiler->get_prompt());
          }
          std::this_thread::sleep_for(std::chrono::microseconds(50000));
        }
      });
    }
    repl_startup_func();

    // Poll Terminal
    while (status != ReplStatus::WANT_EXIT) {
      if (status == ReplStatus::WANT_RELOAD) {
        lg::info("Reloading compiler...");
        std::lock_guard<std::mutex> lock(compiler_mutex);
        if (compiler) {
          compiler->save_repl_history();
        }
        compiler = std::make_unique<Compiler>(
            game_version, instruction_set, std::make_optional(repl_config), username,
            std::make_unique<REPL::Wrapper>(username, repl_config, startup_file, nrepl_server_ok));
        status = ReplStatus::OK;
      }
      // process user input
      std::string input_from_stdin = compiler->get_repl_input();
      if (!input_from_stdin.empty()) {
        // lock, while we compile
        std::lock_guard<std::mutex> lock(compiler_mutex);
        status = compiler->handle_repl_string(input_from_stdin);
      }
    }
  } catch (std::exception& e) {
    lg::error("Compiler Fatal Error: {}", e.what());
    status = ReplStatus::WANT_EXIT;
  }

  // TODO - investigate why there is such a delay when exitting

  // Cleanup
  if (nrepl_server_ok) {
    repl_server.shutdown_server();
    nrepl_thread.join();
  }
  return 0;
}
