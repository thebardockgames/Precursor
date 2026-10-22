package com.opengoalandroid.jak1;

import android.content.Intent;
import android.content.pm.ActivityInfo;
import android.os.Bundle;
import android.os.Build;
import android.view.ViewGroup;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import java.io.File;
import org.libsdl.app.SDLActivity;

/**
 * Passes --proj-path pointing at this app's *internal* files dir (getFilesDir(), a plain
 * Unix-permission directory this app's own uid owns outright - no scoped-storage/FUSE layer in
 * front of it) to the native main() (game/main.cpp), the same way the headless
 * scripts/android-port/cmake-poc executable is given --proj-path on the command line via
 * `adb shell`. The default SDLActivity (org.libsdl.app.SDLActivity, used before this class
 * existed) passes no arguments at all, so main() falls back to its "look for data/ next to my own
 * executable" auto-detection - which doesn't work at all for a JNI-loaded shared library (it
 * resolves to Android's own /system/bin/app_process64, not this app's libmain.so - see
 * the development notes' "Android GLES 3.2 renderer port" section for how this was found and
 * confirmed, not guessed, via a stdout-to-logcat redirect added to main.cpp for exactly this
 * purpose).
 *
 * Previously used getExternalFilesDir() (app-specific external storage) instead - switched away
 * from it after a real device (a freshly-reinstalled app, different debug-keystore signature than
 * whatever session originally populated /sdcard/Android/data/<pkg>) could not open its own
 * `adb push`-written files there (`EACCES`, both from the real game process and from a `run-as`
 * probe) even immediately after a full reboot - some MIUI/HyperOS-specific scoped-storage
 * inconsistency for adb-shell-written content under another app's external directory, not a bug
 * in this app's own code. getFilesDir() sidesteps it entirely: it's a real directory with normal
 * Unix ownership, reachable from `adb shell` via `run-as` for pushing data (stage under
 * /data/local/tmp, then `run-as <pkg> cp -r ...` to get real ownership), with no FUSE consulted at
 * all.
 */
public class Jak1Activity extends SDLActivity {
    private TouchControlsView touchControls;
    private boolean activityResumed;
    private String selectedGame;
    private File selectedData;

    private static native void nativeSetAudioActive(boolean active);
    private static native void nativeFlushLog();
    private static native long nativeGetGameFrameCount();

    @Override
    protected boolean sendCommand(int command, Object data) {
        // Keep SDL's own fullscreen bookkeeping/resize notifications consistent.
        // A desktop window-mode setting must not restore Android bars. (AI-assisted)
        return super.sendCommand(command, command == COMMAND_CHANGE_WINDOW_STYLE ? 1 : data);
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        if (touchControls != null) nativeSetAudioActive(activityResumed && hasFocus && (gameMenu == null || !gameMenu.isOpen()));
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus && Build.VERSION.SDK_INT >= 30) {
            WindowInsetsController controller = getWindow().getInsetsController();
            if (controller != null) {
                controller.setSystemBarsBehavior(
                        WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
                controller.hide(WindowInsets.Type.systemBars());
            }
        }
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        if (BuildConfig.OPENGOAL_LAUNCHER) {
            selectedGame = getIntent().getStringExtra("game");
            selectedData = LauncherStore.installed(this, selectedGame);
            if (selectedData == null || LauncherStore.installing(this)) {
                // Do not initialize SDL against incomplete or missing data.
                startActivity(new android.content.Intent(this, LauncherActivity.class));
                super.onCreate(savedInstanceState);
                finish();
                return;
            }
        }
        if (BuildConfig.OPENGOAL_LAUNCHER) writeSession();
        super.onCreate(savedInstanceState);
        // SDL has loaded libmain before creating mLayout. (AI-assisted)
        if (mLayout != null) {
            touchControls = new TouchControlsView(this);
            touchControls.setOnMenu(this::showGameMenu);
            mLayout.addView(touchControls, new ViewGroup.LayoutParams(
                    ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
            nativeSetAudioActive(false);
            applyTouchMode();
            String name = BuildConfig.OPENGOAL_LAUNCHER ? InstallService.title(selectedGame) : getTitle().toString();
            // Loading screen until the game draws real frames, then the menu above everything.
            mLayout.addView(new LoadingView(this, name, Jak1Activity::gameFrames), new ViewGroup.LayoutParams(
                    ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
            gameMenu = new GameMenuView(this, name,
                    BuildConfig.OPENGOAL_LAUNCHER ? "Volver a Precursor" : "Salir del juego",
                    () -> { if (BuildConfig.OPENGOAL_LAUNCHER) returnToLauncher(); else finish(); },
                    shown -> {
                        if (touchControls != null) touchControls.releaseAll();
                        // The game keeps running underneath; at least its sound stops while choosing.
                        nativeSetAudioActive(!shown && activityResumed && getWindow().getDecorView().hasWindowFocus());
                    });
            mLayout.addView(gameMenu, new ViewGroup.LayoutParams(
                    ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
        }
    }

    private static long gameFrames() {
        try {
            return nativeGetGameFrameCount();
        } catch (UnsatisfiedLinkError notLoaded) {
            return 0;
        }
    }

    /**
     * Touch controls follow the launcher's "Controles táctiles" option: automatic (hidden while a
     * gamepad is connected), always visible, or hidden. (AI-assisted)
     */
    private void applyTouchMode() {
        String mode = getSharedPreferences(GameOptionsActivity.PREFS, MODE_PRIVATE)
                .getString(GameOptionsActivity.TOUCH_MODE, "auto");
        if ("always".equals(mode)) { touchControls.setControlsVisible(true); return; }
        if ("never".equals(mode)) { touchControls.setControlsVisible(false); return; }
        touchControls.setControlsVisible(Gamepad.connected() == null);
        if (padListener == null) {
            padListener = new android.hardware.input.InputManager.InputDeviceListener() {
                @Override public void onInputDeviceAdded(int id) { touchControls.setControlsVisible(Gamepad.connected() == null); }
                @Override public void onInputDeviceRemoved(int id) { touchControls.setControlsVisible(Gamepad.connected() == null); }
                @Override public void onInputDeviceChanged(int id) { }
            };
            getSystemService(android.hardware.input.InputManager.class).registerInputDeviceListener(padListener, null);
        }
    }

    private android.hardware.input.InputManager.InputDeviceListener padListener;

    @Override
    protected void onResume() {
        super.onResume();
        activityResumed = true;
        if (touchControls != null) {
            nativeSetAudioActive(getWindow().getDecorView().hasWindowFocus() && (gameMenu == null || !gameMenu.isOpen()));
        }
    }

    @Override
    protected void onPause() {
        activityResumed = false;
        if (touchControls != null) {
            touchControls.releaseAll();
            // Stop Cubeb before SDL suspends its own threads. (AI-assisted)
            nativeSetAudioActive(false);
        }
        super.onPause();
    }

    @Override
    protected void onStop() {
        if (touchControls != null) nativeSetAudioActive(false);
        super.onStop();
    }

    @Override
    public void setOrientationBis(int width, int height, boolean resizable, String hint) {
        // Keep SDL window creation/resizing from overriding the manifest's landscape lock.
        // This changes only the game, not the phone's rotation setting. (AI-assisted)
        setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE);
    }

    @Override
    protected String[] getArguments() {
        String game = BuildConfig.OPENGOAL_LAUNCHER ? selectedGame : BuildConfig.OPENGOAL_GAME;
        File dataDirFile = BuildConfig.OPENGOAL_LAUNCHER ? selectedData : new File(getFilesDir(), "data");
        File configDirFile = new File(getFilesDir(), BuildConfig.OPENGOAL_LAUNCHER
                ? "config/" + game : "config");
        dataDirFile.mkdirs();
        configDirFile.mkdirs();
        String dataDir = dataDirFile.getAbsolutePath();
        // Also needed: get_user_config_dir() (common/util/FileUtil.cpp) falls into its #elif
        // __linux branch on Android (bionic reports as Linux) and resolves a *relative* path
        // there (get_user_home_dir() returns empty with no real $HOME set in this JNI context),
        // which then fails outright ("Read-only file system") once main() has actually gotten
        // far enough to try - confirmed via the same stdout-to-logcat redirect used to find the
        // --proj-path issue, not guessed. --config-path (already a real flag, game/main.cpp) is
        // a zero-C++-change fix - reuses the same writable internal-files location.
        String configDir = configDirFile.getAbsolutePath();
        return new String[]{"-g", game,
                "--proj-path", dataDir, "--config-path", configDir};
    }

    private GameMenuView gameMenu;
    private boolean selectDown, startDown;

    /** Back (button or gesture) opens the game menu, or closes it when it is open. (AI-assisted) */
    @Override
    public void onBackPressed() {
        if (gameMenu != null && gameMenu.isOpen()) gameMenu.close();
        else showGameMenu();
    }

    /** While the menu is open the sticks must not move Jak underneath it. */
    @Override
    public boolean dispatchGenericMotionEvent(android.view.MotionEvent event) {
        if (gameMenu != null && gameMenu.isOpen()) return true;
        return super.dispatchGenericMotionEvent(event);
    }

    /**
     * Select + Start together opens the game menu from a controller. Both buttons still reach the
     * game, so Start keeps opening Jak's own pause menu. (AI-assisted)
     */
    @Override
    public boolean dispatchKeyEvent(android.view.KeyEvent event) {
        if (gameMenu != null && gameMenu.isOpen()) return gameMenu.handleKey(event);
        int code = event.getKeyCode();
        if (code == android.view.KeyEvent.KEYCODE_BUTTON_SELECT || code == android.view.KeyEvent.KEYCODE_BUTTON_START) {
            boolean down = event.getAction() == android.view.KeyEvent.ACTION_DOWN;
            if (code == android.view.KeyEvent.KEYCODE_BUTTON_SELECT) selectDown = down; else startDown = down;
            if (down && selectDown && startDown && event.getRepeatCount() == 0) {
                selectDown = startDown = false;
                showGameMenu();
                return true;
            }
        }
        return super.dispatchKeyEvent(event);
    }

    private void showGameMenu() {
        if (gameMenu == null) {
            if (BuildConfig.OPENGOAL_LAUNCHER) returnToLauncher(); else finish();
            return;
        }
        gameMenu.open();
    }

    /**
     * Ends the game and shows the launcher. The runtimes hold process-global state, so the game
     * process is ended rather than kept around; the session marker goes first so the launcher
     * does not report this as a crash. (AI-assisted)
     */
    private void returnToLauncher() {
        startActivity(new Intent(this, LauncherActivity.class)
                        .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TOP | Intent.FLAG_ACTIVITY_SINGLE_TOP),
                android.app.ActivityOptions.makeCustomAnimation(this, R.anim.launcher_return, R.anim.hold).toBundle());
        new android.util.AtomicFile(LauncherStore.session(this)).delete();
        try {
            nativeFlushLog();
        } catch (UnsatisfiedLinkError ignored) {
            // libmain was never loaded (the game never started): nothing to flush.
        }
        android.os.Process.killProcess(android.os.Process.myPid());
    }

    /** Lets the launcher tell a crash from a normal exit and know which game is open. (AI-assisted) */
    private void writeSession() {
        try {
            LauncherStore.write(LauncherStore.session(this), new org.json.JSONObject()
                    .put("game", selectedGame).put("pid", android.os.Process.myPid())
                    .put("started", System.currentTimeMillis()).put("commit", BuildConfig.GIT_COMMIT));
        } catch (Exception error) {
            android.util.Log.w("OpenGOAL", "Cannot write session marker", error);
        }
    }

    /**
     * This activity is singleInstance, so opening another game while one runs in the background
     * arrives here instead of creating a new activity. The runtimes hold process-global state, so
     * hand the request to the launcher and end this process. (AI-assisted)
     */
    @Override protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        String requested = intent.getStringExtra("game");
        if (!BuildConfig.OPENGOAL_LAUNCHER || requested == null || requested.equals(selectedGame)) return;
        startActivity(new Intent(this, LauncherActivity.class)
                .putExtra(LauncherActivity.EXTRA_LAUNCH_GAME, requested)
                .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TOP | Intent.FLAG_ACTIVITY_SINGLE_TOP));
        new android.util.AtomicFile(LauncherStore.session(this)).delete();
        android.os.Process.killProcess(android.os.Process.myPid());
    }

    @Override protected void onDestroy() {
        if (padListener != null) getSystemService(android.hardware.input.InputManager.class).unregisterInputDeviceListener(padListener);
        if (BuildConfig.OPENGOAL_LAUNCHER) new android.util.AtomicFile(LauncherStore.session(this)).delete();
        super.onDestroy();
        if (BuildConfig.OPENGOAL_LAUNCHER) {
            // All three runtimes contain process-global state. Start each session fresh.
            android.os.Process.killProcess(android.os.Process.myPid());
        }
    }
}
