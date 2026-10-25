#include <jni.h>
#include <cstdio>
#include <stdexcept>
#include <unistd.h>
#include <cerrno>
#include "iso_probe.h"

extern "C" JNIEXPORT jstring JNICALL
Java_com_opengoalandroid_jak1_InstallService_nativeProbe(JNIEnv* env, jobject, jint descriptor, jstring expected) {
  try {
    if (lseek(descriptor, 0, SEEK_SET) < 0) {
      if (errno == ESPIPE) return nullptr; // Cloud providers may require a private, seekable copy.
      throw std::runtime_error("No se pudo acceder a la ISO seleccionada");
    }
    int copy = dup(descriptor);
    if (copy < 0) throw std::runtime_error("No se pudo abrir la ISO");
    std::unique_ptr<FILE, decltype(&fclose)> fp(fdopen(copy, "rb"), &fclose);
    if (!fp) { close(copy); throw std::runtime_error("No se pudo leer la ISO"); }
    const char* value = env->GetStringUTFChars(expected, nullptr);
    if (!value) return nullptr;
    std::string game(value);
    env->ReleaseStringUTFChars(expected, value);
    const auto info = probe_game_iso(fp.get(), game);
    return env->NewStringUTF(info.game_name.c_str());
  } catch (const std::exception& error) {
    env->ThrowNew(env->FindClass("java/io/IOException"), error.what());
    return nullptr;
  }
}

#include "common/log/log.h"
#include "decompiler/extractor/pipeline.h"

// One invocation per :installer process, separate from SDL and its global state. (AI-assisted)
extern "C" JNIEXPORT jstring JNICALL
Java_com_opengoalandroid_jak1_InstallService_nativeInstall(
    JNIEnv* env, jobject service, jstring project, jstring iso, jstring log, jstring expected,
    jstring decomp_config) {
  auto string_value = [env](jstring value) {
    const char* chars = env->GetStringUTFChars(value, nullptr);
    if (!chars) throw std::runtime_error("No se pudo leer la ruta");
    std::string result(chars);
    env->ReleaseStringUTFChars(value, chars);
    return result;
  };
  try {
    const fs::path root = string_value(project);
    const fs::path input = string_value(iso);
    const auto logfile = string_value(log);
    const auto expected_game = string_value(expected);
    // Decompiler config override sized by InstallService for this phone's free RAM. (AI-assisted)
    const auto config_override = string_value(decomp_config);
    if (!freopen(logfile.c_str(), "a", stdout) || !freopen(logfile.c_str(), "a", stderr)) {
      throw std::runtime_error("No se pudo abrir el registro");
    }
    setvbuf(stdout, nullptr, _IOLBF, 0);
    setvbuf(stderr, nullptr, _IOLBF, 0);
    fs::current_path(root);
    file_util::setup_project_path(root);
    file_util::override_user_config_dir(root / "install-config", true);
    lg::initialize();
    lg::disable_ansi_colors();
    auto cls = env->GetObjectClass(service);
    auto callback = env->GetMethodID(cls, "onNativeProgress",
                                    "(Ljava/lang/String;Ljava/lang/String;)V");
    env->DeleteLocalRef(cls);
    if (!callback) return nullptr;
    auto progress = [&](const std::string& game, const char* message) {
      auto jgame = env->NewStringUTF(game.c_str());
      auto jmessage = env->NewStringUTF(message);
      env->CallVoidMethod(service, callback, jgame, jmessage);
      env->DeleteLocalRef(jgame);
      env->DeleteLocalRef(jmessage);
      if (env->ExceptionCheck()) throw std::runtime_error("No se pudo actualizar el progreso");
    };
    const auto [valid_iso, iso_error] = is_iso_file(input);
    if (!valid_iso) throw std::runtime_error("La ISO está incompleta o no es válida");
    progress(expected_game, "Comprobando el juego y la edición de la ISO…");
    {
      std::unique_ptr<FILE, decltype(&fclose)> fp(file_util::open_file(input, "rb"), &fclose);
      if (!fp) throw std::runtime_error("No se pudo abrir la copia de la ISO");
      probe_game_iso(fp.get(), expected_game);
    }
    progress(expected_game, "Extrayendo y verificando la ISO…");
    const auto extracted = root / "iso_data" / "input";
    const auto layout = extract_files(input, extracted);
    const auto [serial, hash] = findElfFile(extracted);
    if (!serial || !hash) throw std::runtime_error("La ISO no contiene un juego Jak reconocido");
    const BuildInfo info{*serial, *hash};
    const auto version = get_version_info_from_build_info(info);
    if (!version) throw std::runtime_error("Esta edición de la ISO no está soportada");
    const auto [contents_hash, count] = calculate_extraction_hash(layout);
    const auto [verified, code] = validate(expected_game, extracted, contents_hash, count);
    if (!verified || code != ExtractorErrorCode::SUCCESS) {
      throw std::runtime_error("La verificación de la ISO falló. Consulta el registro");
    }
    const auto game = verified->game_name;
    const auto data = root / "iso_data" / game;
    fs::rename(extracted, data);
    const nlohmann::json buildinfo{info};
    file_util::write_text_file((data / "buildinfo.json").string(), buildinfo.dump(2));
    fs::remove(input);  // Only our private copy, never the selected document.
    progress(game, "Preparando escenarios, texturas y audio…");
    lg::info("Decompiler config override: {}", config_override);
    if (decompile(data, game, config_override) != ExtractorErrorCode::SUCCESS) {
      throw std::runtime_error("No se pudieron preparar los recursos del juego");
    }
    progress(game, "Compilando el juego para el teléfono…");
    if (compile(data, game, emitter::InstructionSet::ARM64) != ExtractorErrorCode::SUCCESS) {
      throw std::runtime_error("La compilación del juego falló");
    }
    for (const auto* name : {"KERNEL.CGO", "GAME.CGO"}) {
      const auto output = root / "out" / game / "iso" / name;
      if (!fs::is_regular_file(output) || fs::file_size(output) == 0) {
        throw std::runtime_error("Faltan archivos compilados del juego");
      }
    }
    const auto levels = root / "out" / game / "fr3";
    if (!fs::is_directory(levels) || fs::is_empty(levels)) {
      throw std::runtime_error("Faltan los escenarios del juego");
    }
    fflush(stdout);
    return env->NewStringUTF(game.c_str());
  } catch (const std::exception& error) {
    fprintf(stderr, "Installer: %s\n", error.what());
    fflush(stderr);
    if (!env->ExceptionCheck()) env->ThrowNew(env->FindClass("java/io/IOException"), error.what());
    return nullptr;
  }
}
