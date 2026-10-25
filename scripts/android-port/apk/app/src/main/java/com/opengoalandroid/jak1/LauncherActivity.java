package com.opengoalandroid.jak1;

import android.animation.ValueAnimator;
import android.app.*;
import android.content.*;
import android.content.res.Configuration;
import android.graphics.*;
import android.graphics.drawable.GradientDrawable;
import android.hardware.input.InputManager;
import android.net.Uri;
import android.os.*;
import android.text.format.DateUtils;
import android.view.*;
import android.widget.*;
import org.json.JSONObject;
import java.io.*;
import java.util.*;

/**
 * Precursor's launcher (powered by OpenGOAL, unofficial). Two screens:
 *  - a guided, animated setup that takes the player from "welcome" to a ready game (also used to
 *    add another game later), driven by the real installer's state;
 *  - the "Estante" shelf: the selected game's art and actions on top, a row with the three games
 *    and the Settings/Report tiles below. It stacks vertically in portrait.
 * Every screen works with touch, a gamepad (d-pad/stick, A/B/X/Y, LB/RB, Start/Select) and a
 * keyboard. (AI-assisted)
 */
public final class LauncherActivity extends Activity {
    private static final int PICK_ISO = 10, EXPORT_LOG = 11, SETTINGS = 12;
    /** Set by Jak1Activity when another game was requested while one was running. */
    static final String EXTRA_LAUNCH_GAME = "launchGame";
    private static final int GLES_3_2 = 0x30002;
    private static final String[] GAMES = {"jak1", "jak2", "jak3"};
    private static final int[] COVERS = {R.drawable.cover_jak1, R.drawable.cover_jak2_playstation, R.drawable.cover_jak3};
    /** Approximate retail ISO sizes, used to show how much free space the preparation needs. */
    private static final double[] ISO_GB = {1.46, 4.41, 4.52};

    private enum Screen { WIZARD, SHELF }
    private static final int W_WELCOME = 0, W_PICK = 1, W_ISO = 2, W_CHECK = 3, W_PREPARE = 4, W_DONE = 5, W_ERROR = 6;

    private final Handler handler = new Handler(Looper.getMainLooper());
    /** The language this screen was built in; Settings may change it meanwhile. */
    private String createdLanguage = "";
    private FrameLayout root;
    private Screen screen = Screen.SHELF;
    private int wizardStep = W_WELCOME, wizardGame = 0, selected = 0;
    private boolean wizardFirstRun, helpOpen, crashPromptShown;
    private long startingUntil;
    private String errorText = "";
    // live views updated by the poller without rebuilding the screen
    private OrbView progressOrb;
    private GlowBar progressBar;
    private TextView progressText;
    private LinearLayout stagesBox;
    private int shownStage = -2, shownPercent;
    private ValueAnimator percentAnim;
    // Motion: which way the next wizard step slides in, the step the dots last showed, and
    // whether the next shelf build plays its entrance. (AI-assisted)
    private LinearLayout wizardText;
    private int wizardDir, lastDotsStep = -1;
    private boolean animateNextShelf = true, heroAnimate, transitioning, launchedGame;
    // shelf views updated when the selection changes
    private LinearLayout heroBox;
    private ImageView heroArt, backgroundArt;
    private final View[] tiles = new View[3];
    private InputManager.InputDeviceListener padListener;

    private final Runnable poll = new Runnable() {
        @Override public void run() { pollInstaller(); handler.postDelayed(this, 700); }
    };

    // ------------------------------------------------------------------ lifecycle

    /** Precursor's language: the phone's, or the one picked in Settings. (AI-assisted) */
    @Override protected void attachBaseContext(Context base) { super.attachBaseContext(Language.wrap(base)); }

    @Override public void onCreate(Bundle saved) {
        super.onCreate(saved);
        if (!BuildConfig.OPENGOAL_LAUNCHER) {
            startActivity(new Intent(this, Jak1Activity.class));
            finish();
            return;
        }
        createdLanguage = Language.current(this);
        root = new FrameLayout(this);
        root.setBackgroundColor(UiKit.BACKGROUND);
        setContentView(root);
        UiKit.immersive(this);
        selected = clampGame(getPreferences(0).getInt("selected", 0));
        if (saved != null) {
            screen = Screen.valueOf(saved.getString("screen", Screen.SHELF.name()));
            wizardStep = saved.getInt("wizardStep", W_WELCOME);
            wizardGame = clampGame(saved.getInt("wizardGame", 0));
            wizardFirstRun = saved.getBoolean("wizardFirstRun", false);
            selected = clampGame(saved.getInt("selected", selected));
            errorText = saved.getString("errorText", "");
        } else {
            chooseStartScreen();
        }
        rebuild();
        new Thread(() -> {
            ReportBuilder.noteInstallerExit(this); // before a new installation overwrites install.log
            LauncherStore.discardStaleWork(this);
            LauncherStore.discardUnusedTools(this, currentTools());
            Updater.discardDownloads(this);
        }, "Discard stale install").start();
        handleLaunchRequest(getIntent());
        UpdateFlow.handleInstallStatus(this, getIntent());
    }

    @Override protected void onSaveInstanceState(Bundle state) {
        state.putString("screen", screen.name());
        state.putInt("wizardStep", wizardStep);
        state.putInt("wizardGame", wizardGame);
        state.putBoolean("wizardFirstRun", wizardFirstRun);
        state.putInt("selected", selected);
        state.putString("errorText", errorText);
        super.onSaveInstanceState(state);
    }

    @Override protected void onResume() {
        super.onResume();
        if (root == null) return;
        if (!createdLanguage.equals(Language.current(this))) { recreate(); return; }
        handler.post(poll);
        handler.postDelayed(this::checkLastSession, 500);
        padListener = new InputManager.InputDeviceListener() {
            @Override public void onInputDeviceAdded(int id) { rebuild(); }
            @Override public void onInputDeviceRemoved(int id) { rebuild(); }
            @Override public void onInputDeviceChanged(int id) { }
        };
        getSystemService(InputManager.class).registerInputDeviceListener(padListener, handler);
        if (launchedGame) { launchedGame = false; animateNextShelf = true; transitioning = false; }
        if (screen == Screen.SHELF) rebuild(); // saves or installs may have changed meanwhile
        handler.postDelayed(this::showUpdatedNotice, 700);
        UpdateFlow.resume(this);
        if (screen == Screen.SHELF && Updater.dueForCheck(this)) UpdateFlow.check(this, false);
    }

    @Override protected void onPause() {
        handler.removeCallbacks(poll);
        if (padListener != null) getSystemService(InputManager.class).unregisterInputDeviceListener(padListener);
        super.onPause();
    }

    @Override protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        setIntent(intent);
        handleLaunchRequest(intent);
        UpdateFlow.handleInstallStatus(this, intent);
    }

    private void chooseStartScreen() {
        JSONObject job = LauncherStore.read(LauncherStore.job(this));
        String pendingDone = getPreferences(0).getString("pendingDone", "");
        if (LauncherStore.installing(this) && "install".equals(job.optString("operation"))) {
            screen = Screen.WIZARD;
            wizardGame = gameIndex(job.optString("game"));
            wizardStep = InstallProgress.stage(job) < 0 ? W_CHECK : W_PREPARE;
        } else if (!pendingDone.isEmpty() && LauncherStore.installed(this, pendingDone) != null) {
            screen = Screen.WIZARD;
            wizardGame = gameIndex(pendingDone);
            wizardStep = W_DONE;
        } else if (installedCount() == 0) {
            screen = Screen.WIZARD;
            wizardFirstRun = true;
            wizardStep = W_WELCOME;
        } else {
            screen = Screen.SHELF;
        }
    }

    private void rebuild() {
        if (root == null) return;
        View focused = getCurrentFocus();
        int focusId = focused == null ? View.NO_ID : focused.getId();
        root.removeAllViews();
        progressOrb = null; progressBar = null; progressText = null; stagesBox = null; shownStage = -2;
        wizardText = null;
        if (percentAnim != null) percentAnim.cancel();
        root.addView(screen == Screen.WIZARD ? buildWizard() : buildShelf(), new FrameLayout.LayoutParams(-1, -1));
        View again = focusId == View.NO_ID ? null : root.findViewById(focusId);
        if (again != null) again.requestFocus();
        // With a gamepad or keyboard (not in touch mode) something must always be selected.
        else if (!root.isInTouchMode()) root.post(() -> root.restoreDefaultFocus());
    }

    // ------------------------------------------------------------------ guided setup

    private View buildWizard() {
        boolean wide = wide();
        FrameLayout frame = new FrameLayout(this);
        GradientDrawable bg = new GradientDrawable();
        bg.setGradientType(GradientDrawable.RADIAL_GRADIENT);
        bg.setColors(new int[]{0xFF1B4550, 0xFF0E1C24, 0xFF0A1218});
        bg.setGradientCenter(wide ? .74f : .5f, wide ? .45f : .3f);
        bg.setGradientRadius(dp(wide ? 520 : 460));
        frame.setBackground(bg);

        OrbView.Mode mode;
        switch (wizardStep) {
            case W_WELCOME: mode = OrbView.Mode.ASSEMBLE; break;
            case W_CHECK: mode = OrbView.Mode.SCAN; break;
            case W_PREPARE: mode = OrbView.Mode.PROGRESS; break;
            case W_DONE: mode = OrbView.Mode.DONE; break;
            case W_ERROR: mode = OrbView.Mode.ERROR; break;
            default: mode = OrbView.Mode.IDLE; break;
        }
        OrbView orbView = new OrbView(this, mode);
        if (wizardStep == W_PREPARE) progressOrb = orbView;
        if (wizardStep == W_ERROR) orbView.setProgress(42);
        // The orb sits in a box so the finished game's cover can come out of it.
        FrameLayout orb = new FrameLayout(this);
        orb.setClipChildren(false);
        orb.addView(orbView, new FrameLayout.LayoutParams(-1, -1));
        if (wizardStep == W_DONE) emergeCover(orb, orbView);

        LinearLayout text = column();
        wizardText = text;
        text.setGravity(wide ? Gravity.START : Gravity.CENTER_HORIZONTAL);
        fillWizardText(text, wide);

        if (wide) {
            LinearLayout row = new LinearLayout(this);
            row.setGravity(Gravity.CENTER_VERTICAL);
            row.setPadding(dp(36), dp(20), dp(36), dp(40));
            LinearLayout left = column();
            left.setGravity(Gravity.CENTER_VERTICAL);
            left.addView(lockup(20));
            LinearLayout.LayoutParams textParams = new LinearLayout.LayoutParams(-1, -2);
            textParams.topMargin = dp(20);
            left.addView(text, textParams);
            // Scrollable so low screens never hide the buttons.
            ScrollView leftScroll = new ScrollView(this);
            leftScroll.setFillViewport(true);
            leftScroll.addView(left, new ScrollView.LayoutParams(-1, -2));
            row.addView(leftScroll, new LinearLayout.LayoutParams(0, -1, 1.15f));
            int orbSize = Math.min(screenHeight() - dp(110), dp(260));
            LinearLayout.LayoutParams orbParams = new LinearLayout.LayoutParams(0, orbSize, .85f);
            row.addView(orb, orbParams);
            row.setClipChildren(false);
            frame.addView(row, new FrameLayout.LayoutParams(-1, -1));
        } else {
            LinearLayout col = column();
            col.setGravity(Gravity.CENTER);
            col.setPadding(dp(28), dp(24), dp(28), dp(56));
            int orbSize = Math.min(screenWidth() / 2, dp(240));
            col.addView(orb, new LinearLayout.LayoutParams(orbSize, orbSize));
            col.setClipChildren(false);
            LinearLayout.LayoutParams textParams = new LinearLayout.LayoutParams(-1, -2);
            textParams.topMargin = dp(24);
            col.addView(text, textParams);
            ScrollView scroll = new ScrollView(this);
            scroll.setFillViewport(true);
            scroll.addView(col, new ScrollView.LayoutParams(-1, -1));
            frame.addView(scroll, new FrameLayout.LayoutParams(-1, -1));
        }
        addHints(frame, wizardHints());
        frame.setClipChildren(false);
        // Steps slide in from the side they come from (forward: right, back: left).
        if (wizardDir != 0) Motion.slideIn(text, wizardDir, 40);
        wizardDir = 0;
        return frame;
    }

    private Object[] wizardHints() {
        switch (wizardStep) {
            case W_WELCOME: return new Object[]{Gamepad.Action.CONFIRM, getString(R.string.hint_start)};
            case W_PICK: case W_ISO: return new Object[]{Gamepad.Action.CONFIRM, getString(R.string.choose), Gamepad.Action.BACK, getString(R.string.back)};
            case W_CHECK: case W_PREPARE: return new Object[]{Gamepad.Action.BACK, getString(R.string.hint_continue_background)};
            case W_DONE: return new Object[]{Gamepad.Action.CONFIRM, getString(R.string.ok), Gamepad.Action.BACK, getString(R.string.menu)};
            default: return new Object[]{Gamepad.Action.CONFIRM, getString(R.string.ok), Gamepad.Action.BACK, getString(R.string.back)};
        }
    }

    private void fillWizardText(LinearLayout box, boolean wide) {
        String game = GAMES[wizardGame];
        String title = InstallService.title(game);
        int gravity = wide ? Gravity.START : Gravity.CENTER_HORIZONTAL;
        if (wizardStep != W_WELCOME && wizardStep != W_ERROR) box.addView(dots(), wrap());
        switch (wizardStep) {
            case W_WELCOME: {
                if (!wide) box.addView(pill(getString(R.string.brand_pill), UiKit.MUTED), wrap());
                box.addView(heading(getString(R.string.welcome_title), wide ? 34 : 30, gravity), topMargin(-1, 10));
                box.addView(body(getString(R.string.welcome_body), gravity), topMargin(-1, 8));
                Button start = primary(getString(R.string.hint_start), v -> goWizard(W_PICK));
                start.setFocusedByDefault(true);
                box.addView(actions(wide, start), topMargin(-1, 22));
                break;
            }
            case W_PICK: {
                box.addView(heading(getString(R.string.pick_title), 28, gravity), topMargin(-1, 12));
                box.addView(body(getString(R.string.pick_body), gravity), topMargin(-1, 6));
                LinearLayout picks = new LinearLayout(this);
                picks.setClipChildren(false);
                for (int i = 0; i < 3; i++) {
                    final int index = i;
                    LinearLayout card = column();
                    card.setGravity(Gravity.CENTER_HORIZONTAL);
                    card.setPadding(dp(6), dp(6), dp(6), dp(6));
                    ImageView cover = coverImage(i, false, 14);
                    int size = wide ? Math.min(dp(120), (screenWidth() / 2 - dp(80)) / 3) : (screenWidth() - dp(110)) / 3;
                    card.addView(cover, new LinearLayout.LayoutParams(size, size));
                    TextView name = label(InstallService.title(GAMES[i]), 13, UiKit.TEXT, true);
                    name.setGravity(Gravity.CENTER);
                    card.addView(name, topMargin(-2, 6));
                    TextView size2 = label(getString(R.string.iso_size, String.format(Locale.ROOT, "%.1f", ISO_GB[i])), 11, UiKit.MUTED, false);
                    size2.setGravity(Gravity.CENTER);
                    card.addView(size2);
                    Gamepad.focusable(card, 16);
                    card.setId(1000 + i);
                    card.setContentDescription(getString(R.string.pick_game, InstallService.title(GAMES[i])));
                    card.setOnClickListener(v -> pickGame(picks, index));
                    if (i == 0) card.setFocusedByDefault(true);
                    Motion.pressable(card);
                    picks.addView(card, new LinearLayout.LayoutParams(0, -2, 1));
                }
                box.addView(picks, topMargin(-1, 16));
                break;
            }
            case W_ISO: {
                box.addView(heading(getString(R.string.iso_title, title), 28, gravity), topMargin(-1, 12));
                box.addView(body(getString(R.string.iso_body), gravity), topMargin(-1, 6));
                double need = ISO_GB[wizardGame] * 3 + 3;
                double free = new StatFs(getFilesDir().getPath()).getAvailableBytes() / 1e9;
                TextView space = body(getString(R.string.iso_space, String.format(Locale.ROOT, "%.0f", Math.ceil(need)),
                        String.format(Locale.ROOT, "%.0f", free)), gravity);
                if (free < need) space.setTextColor(0xFFFF9A80);
                box.addView(space, topMargin(-1, 8));
                if (installed(wizardGame)) {
                    TextView keep = body(getString(R.string.prepare_again_keeps), gravity);
                    keep.setTextColor(UiKit.ACCENT);
                    box.addView(keep, topMargin(-1, 8));
                }
                if (helpOpen) {
                    TextView tips = body(getString(R.string.iso_help), gravity);
                    tips.setTextColor(UiKit.MUTED);
                    tips.setPadding(dp(12), dp(6), 0, dp(6));
                    tips.setBackground(leftRule());
                    box.addView(tips, topMargin(-1, 10));
                }
                Button choose = primary(getString(R.string.iso_choose), v -> pickIsoFor(wizardGame));
                choose.setFocusedByDefault(true);
                Button help = ghost(getString(helpOpen ? R.string.iso_help_hide : R.string.iso_help_show), v -> { helpOpen = !helpOpen; rebuild(); });
                box.addView(actions(wide, choose, help), topMargin(-1, 20));
                break;
            }
            case W_CHECK: {
                box.addView(heading(getString(R.string.check_title), 28, gravity), topMargin(-1, 12));
                box.addView(body(getString(R.string.check_body, title), gravity), topMargin(-1, 6));
                break;
            }
            case W_PREPARE: {
                box.addView(heading(getString(R.string.prepare_title, title), 28, gravity), topMargin(-1, 12));
                progressText = body("", gravity);
                box.addView(progressText, topMargin(-1, 6));
                progressBar = new GlowBar(this, false);
                LinearLayout.LayoutParams barParams = new LinearLayout.LayoutParams(-1, dp(6));
                barParams.topMargin = dp(14);
                box.addView(progressBar, barParams);
                stagesBox = column();
                box.addView(stagesBox, topMargin(wide ? -1 : -2, 10));
                Button background = ghost(getString(R.string.hint_continue_background), v -> moveTaskToBack(true));
                Button cancel = ghost(getString(R.string.cancel), v -> confirmCancel());
                background.setFocusedByDefault(true);
                box.addView(actions(wide, background, cancel), topMargin(-1, 16));
                // Paint the current state now; the poller keeps it updated afterwards.
                JSONObject job = LauncherStore.read(LauncherStore.job(this));
                updateProgress(Math.max(0, InstallProgress.stage(job)), InstallProgress.overall(job));
                break;
            }
            case W_DONE: {
                box.addView(heading(getString(R.string.done_title, title), 30, gravity), topMargin(-1, 12));
                box.addView(body(getString(R.string.done_body), gravity), topMargin(-1, 6));
                Button play = primary(getString(R.string.play_now), v -> { leaveDone(); playGame(game); });
                play.setFocusedByDefault(true);
                Motion.beat(play);
                Button menu = ghost(getString(R.string.go_to_menu), v -> leaveDone());
                box.addView(actions(wide, play, menu), topMargin(-1, 20));
                break;
            }
            default: {
                box.addView(heading(getString(R.string.error_title, title), 28, gravity), topMargin(-1, 4));
                box.addView(body(errorText.isEmpty() ? getString(R.string.error_stopped) : errorText, gravity), topMargin(-1, 8));
                Button retry = primary(getString(R.string.choose_another_iso), v -> goWizard(W_ISO));
                retry.setFocusedByDefault(true);
                Button log = ghost(getString(R.string.view_log), v -> showLog());
                Button report = ghost(getString(R.string.report), v -> askForReport(getString(R.string.report_prepare_failed, title, errorText)));
                box.addView(actions(wide, retry, log, report), topMargin(-1, 20));
            }
        }
    }

    private void goWizard(int step) {
        // Forward steps come in from the right; going back (or retrying after an error) from the left.
        int dir = step == wizardStep ? 0 : step == W_ERROR || (step > wizardStep && wizardStep != W_ERROR) ? 1 : -1;
        wizardStep = step;
        if (step != W_ISO) helpOpen = false;
        View old = wizardText;
        if (old == null || dir == 0 || !Motion.enabled() || screen != Screen.WIZARD) {
            wizardDir = dir;
            rebuild();
            return;
        }
        // The current step leaves toward the other side, then the new one slides in.
        final int direction = dir;
        transitioning = true;
        old.animate().alpha(0f).translationX(-direction * dp(24)).setDuration(140).setStartDelay(0)
                .setInterpolator(Motion.IN).withEndAction(() -> {
                    transitioning = false;
                    wizardDir = direction;
                    rebuild();
                }).start();
    }

    /** The chosen game lifts, the others fade, then the ISO step comes in. */
    private void pickGame(LinearLayout picks, int index) {
        if (transitioning) return;
        transitioning = true;
        wizardGame = index;
        for (int i = 0; i < picks.getChildCount(); i++) {
            View card = picks.getChildAt(i);
            if (i == index) card.animate().scaleX(1.06f).scaleY(1.06f).translationY(-dp(6)).setDuration(220).start();
            else card.animate().alpha(.25f).setDuration(220).start();
        }
        handler.postDelayed(() -> { transitioning = false; goWizard(W_ISO); }, Motion.delay(380));
    }

    /**
     * "Portal": the orb's core becomes the game's cover. It appears round inside the ring,
     * spinning, then opens into a rounded square centred on the orb. (AI-assisted)
     */
    private void emergeCover(FrameLayout box, OrbView orb) {
        ImageView cover = new ImageView(this);
        cover.setImageResource(COVERS[wizardGame]);
        cover.setScaleType(ImageView.ScaleType.CENTER_CROP);
        cover.setElevation(dp(10));
        cover.setAlpha(0f);
        float[] radius = {0f};
        cover.setOutlineProvider(new ViewOutlineProvider() {
            @Override public void getOutline(View view, Outline outline) {
                outline.setRoundRect(0, 0, view.getWidth(), view.getHeight(), radius[0]);
            }
        });
        cover.setClipToOutline(true);
        box.addView(cover, new FrameLayout.LayoutParams(0, 0, Gravity.CENTER));
        box.post(() -> {
            int side = Math.round(Math.min(box.getWidth(), box.getHeight()) * 2f / 3f);
            cover.getLayoutParams().width = side;
            cover.getLayoutParams().height = side;
            cover.requestLayout();
            float round = side / 2f, square = dp(16);
            if (!Motion.enabled()) {
                radius[0] = square;
                cover.setAlpha(1f);
                cover.invalidateOutline();
                return;
            }
            orb.animate().scaleX(1.15f).scaleY(1.15f).setStartDelay(450).setDuration(1100).setInterpolator(Motion.OUT).start();
            ValueAnimator portal = ValueAnimator.ofFloat(0f, 1f).setDuration(1400);
            portal.setStartDelay(450);
            portal.setInterpolator(Motion.OUT);
            portal.addUpdateListener(a -> {
                float t = (float) a.getAnimatedValue(), scale, turn, r;
                if (t < .6f) {
                    float k = t / .6f;
                    cover.setAlpha(Math.min(1f, k * 1.6f));
                    scale = .5f + .35f * k;
                    turn = -360f + 300f * k;
                    r = round;
                } else if (t < .85f) {
                    float k = (t - .6f) / .25f;
                    cover.setAlpha(1f);
                    scale = .85f + .2f * k;
                    turn = -60f + 60f * k;
                    r = round + (square - round) * k;
                } else {
                    float k = (t - .85f) / .15f;
                    scale = 1.05f - .05f * k;
                    turn = 0f;
                    r = square;
                }
                cover.setScaleX(scale);
                cover.setScaleY(scale);
                cover.setRotation(turn);
                radius[0] = r * Math.min(1f, 1f / scale);
                cover.invalidateOutline();
            });
            Motion.cancelOnDetach(cover, portal);
            portal.start();
        });
    }

    private void leaveDone() {
        getPreferences(0).edit().remove("pendingDone").apply();
        selected = wizardGame;
        getPreferences(0).edit().putInt("selected", selected).apply();
        screen = Screen.SHELF;
        wizardFirstRun = false;
        animateNextShelf = true;
        rebuild();
    }

    /** Starts the guided flow at the ISO step for a game picked from the shelf. */
    private void startAdd(int game) {
        if (LauncherStore.installing(this)) {
            showError(getString(R.string.already_preparing));
            return;
        }
        screen = Screen.WIZARD;
        wizardFirstRun = false;
        wizardGame = game;
        wizardStep = W_ISO;
        rebuild();
    }

    private void confirmCancel() {
        new AlertDialog.Builder(this).setTitle(R.string.cancel_prepare_title)
                .setMessage(R.string.cancel_prepare_body)
                .setNegativeButton(R.string.keep_preparing, null)
                .setPositiveButton(R.string.cancel, (d, w) -> startService(new Intent(this, InstallService.class).setAction(InstallService.CANCEL)))
                .show();
    }

    /** Follows install-state.json; moves the wizard between Checking, Preparing, Done and Error. */
    private void pollInstaller() {
        if (screen != Screen.WIZARD || (wizardStep != W_CHECK && wizardStep != W_PREPARE)) return;
        JSONObject job = LauncherStore.read(LauncherStore.job(this));
        boolean running = LauncherStore.installing(this);
        boolean starting = SystemClock.elapsedRealtime() < startingUntil;
        String state = job.optString("state"), message = job.optString("message");
        if (running && "running".equals(state)) {
            startingUntil = 0;
            int stage = InstallProgress.stage(job);
            int step = stage < 0 ? W_CHECK : W_PREPARE;
            if (step != wizardStep) { if (!transitioning) goWizard(step); return; }
            if (step == W_PREPARE) updateProgress(stage, InstallProgress.overall(job));
            return;
        }
        if (starting) return;
        if (transitioning) return;
        if ("ready".equals(state) && GAMES[wizardGame].equals(job.optString("game"))) {
            goWizard(W_DONE);
        } else if ("cancelled".equals(state)) {
            goWizard(W_ISO);
        } else {
            errorText = !"running".equals(state) ? message
                    : ReportBuilder.noteInstallerExit(this) == android.app.ApplicationExitInfo.REASON_LOW_MEMORY
                    ? getString(R.string.prepare_low_memory) : getString(R.string.prepare_interrupted);
            goWizard(W_ERROR);
        }
    }

    private static final int[] STAGE_NAMES = {R.string.stage_copy, R.string.stage_extract, R.string.stage_levels,
            R.string.stage_compile, R.string.stage_finish};

    private void updateProgress(int stage, int overall) {
        if (progressBar != null) progressBar.setProgress(overall);
        if (progressOrb != null) { progressOrb.setProgress(overall); progressOrb.setStagesDone(stage); }
        countPercent(overall);
        if (stagesBox != null && stage != shownStage) {
            boolean advanced = shownStage >= 0 && stage > shownStage;
            shownStage = stage;
            stagesBox.removeAllViews();
            if (wide()) {
                // Landscape has little height: one line instead of the full list.
                stagesBox.addView(label(getString(R.string.stage_of, stage + 1, InstallProgress.STAGE_COUNT,
                        getString(STAGE_NAMES[stage])), 14, UiKit.TEXT, true));
                if (advanced) Motion.slideIn(stagesBox, 1, 0);
                return;
            }
            for (int i = 0; i < InstallProgress.STAGE_COUNT; i++) {
                String mark = i < stage ? "✓  " : i == stage ? "●  " : "○  ";
                TextView row = label(mark + getString(STAGE_NAMES[i]), 13, i < stage ? UiKit.ACCENT : i == stage ? UiKit.TEXT : UiKit.MUTED, i == stage);
                stagesBox.addView(row, topMargin(-2, 3));
                if (i == stage) Motion.pulse(row);
                if (advanced && i == stage - 1) {
                    // The stage that just finished gets its check with a small pop.
                    row.setPivotX(0);
                    row.setScaleX(.85f);
                    row.setScaleY(.85f);
                    row.animate().scaleX(1f).scaleY(1f).setDuration(300)
                            .setInterpolator(new android.view.animation.OvershootInterpolator(2f)).start();
                }
            }
        }
    }

    /** The percentage counts up to each new value instead of jumping. */
    private void countPercent(int overall) {
        if (progressText == null) return;
        TextView target = progressText;
        if (percentAnim != null) percentAnim.cancel();
        if (!Motion.enabled()) { shownPercent = overall; target.setText(getString(R.string.prepare_progress, overall)); return; }
        percentAnim = ValueAnimator.ofInt(shownPercent, overall).setDuration(500);
        percentAnim.addUpdateListener(a -> { shownPercent = (int) a.getAnimatedValue(); target.setText(getString(R.string.prepare_progress, shownPercent)); });
        percentAnim.start();
    }

    // ------------------------------------------------------------------ Estante

    private View buildShelf() {
        boolean wide = wide();
        FrameLayout frame = new FrameLayout(this);
        backgroundArt = new ImageView(this);
        backgroundArt.setScaleType(ImageView.ScaleType.CENTER_CROP);
        if (Build.VERSION.SDK_INT >= 31) backgroundArt.setRenderEffect(RenderEffect.createBlurEffect(dp(28), dp(28), Shader.TileMode.CLAMP));
        frame.addView(backgroundArt, new FrameLayout.LayoutParams(-1, -1));
        View shade = new View(this);
        shade.setBackgroundColor(0xB8101820);
        frame.addView(shade, new FrameLayout.LayoutParams(-1, -1));
        OrbView mark = new OrbView(this, OrbView.Mode.MARK);
        mark.setAlpha(.14f);
        int markSize = dp(wide ? 330 : 300);
        FrameLayout.LayoutParams markParams = new FrameLayout.LayoutParams(markSize, markSize, Gravity.BOTTOM | Gravity.END);
        markParams.rightMargin = -markSize / 5;
        markParams.bottomMargin = -markSize / 3;
        frame.addView(mark, markParams);

        LinearLayout col = column();
        col.setPadding(dp(wide ? 32 : 20), dp(wide ? 10 : 22), dp(wide ? 32 : 20), dp(wide ? 26 : 40));
        col.addView(lockup(wide ? 16 : 22));

        heroBox = column();
        heroArt = coverImage(selected, false, 18);
        if (wide) {
            LinearLayout row = new LinearLayout(this);
            row.setGravity(Gravity.CENTER_VERTICAL);
            // Scrolls instead of squashing the buttons when the screen is short.
            ScrollView heroScroll = new ScrollView(this);
            heroScroll.setVerticalScrollBarEnabled(false);
            heroScroll.setFillViewport(true);
            heroBox.setGravity(Gravity.CENTER_VERTICAL);
            heroScroll.addView(heroBox, new ScrollView.LayoutParams(-1, -2));
            row.addView(heroScroll, new LinearLayout.LayoutParams(0, -1, 1));
            int art = Math.min(dp(210), screenHeight() * 2 / 5);
            LinearLayout.LayoutParams artParams = new LinearLayout.LayoutParams(art, art);
            artParams.leftMargin = dp(24);
            heroArt.setElevation(dp(10));
            row.addView(heroArt, artParams);
            LinearLayout.LayoutParams rowParams = new LinearLayout.LayoutParams(-1, 0, 1);
            rowParams.topMargin = dp(8);
            col.addView(row, rowParams);
        } else {
            FrameLayout card = new FrameLayout(this);
            card.setBackground(UiKit.shape(this, UiKit.PANEL, 22));
            card.setClipToOutline(true);
            card.addView(heroArt, new FrameLayout.LayoutParams(-1, -1));
            heroArt.setBackground(null);
            heroArt.setClipToOutline(false);
            View fade = new View(this);
            GradientDrawable gradient = new GradientDrawable(GradientDrawable.Orientation.TOP_BOTTOM,
                    new int[]{0x10050A0E, 0x33050A0E, 0xF2050A0E});
            fade.setBackground(gradient);
            card.addView(fade, new FrameLayout.LayoutParams(-1, -1));
            heroBox.setPadding(dp(18), dp(18), dp(18), dp(18));
            card.addView(heroBox, new FrameLayout.LayoutParams(-1, -2, Gravity.BOTTOM));
            LinearLayout.LayoutParams cardParams = new LinearLayout.LayoutParams(-1, 0, 1);
            cardParams.topMargin = dp(18);
            col.addView(card, cardParams);
            TextView label = label(getString(R.string.your_games), 12, UiKit.MUTED, true);
            label.setLetterSpacing(.1f);
            col.addView(label, topMargin(-2, 18));
        }

        HorizontalScrollView scroll = new HorizontalScrollView(this);
        scroll.setHorizontalScrollBarEnabled(false);
        scroll.setClipToPadding(false);
        scroll.setPadding(dp(4), dp(10), dp(4), dp(4));
        LinearLayout shelf = new LinearLayout(this);
        shelf.setGravity(Gravity.BOTTOM);
        int tile = wide ? Math.min(dp(96), screenHeight() / 5) : (screenWidth() - dp(40)) * 27 / 100;
        for (int i = 0; i < 3; i++) shelf.addView(gameTile(i, tile), tileParams(tile, i == 0 ? 0 : 12));
        View sep = new View(this);
        sep.setBackgroundColor(0x2EFFFFFF);
        LinearLayout.LayoutParams sepParams = new LinearLayout.LayoutParams(dp(2), tile - dp(20));
        sepParams.leftMargin = dp(14); sepParams.rightMargin = dp(2); sepParams.bottomMargin = dp(10);
        shelf.addView(sep, sepParams);
        shelf.addView(utilityTile("⚙", getString(R.string.settings), tile, v -> showSettings()), tileParams(tile * 8 / 10, 12));
        shelf.addView(utilityTile("🐞", getString(R.string.report), tile, v -> askForReport(null)), tileParams(tile * 8 / 10, 12));
        scroll.addView(shelf);
        col.addView(scroll, topMargin(-1, wide ? 10 : 6));

        frame.addView(col, new FrameLayout.LayoutParams(-1, -1));
        addHints(frame, Gamepad.Action.CONFIRM, getString(installed(selected) ? R.string.hint_play : R.string.hint_add),
                Gamepad.Action.OPTIONS, getString(R.string.hint_options), Gamepad.Action.PREV, "",
                Gamepad.Action.NEXT, getString(R.string.hint_switch_game));
        heroAnimate = false;
        fillHero();
        if (animateNextShelf) {
            animateNextShelf = false;
            Motion.stagger(col, 60, 40);
            backgroundArt.setAlpha(0f);
            backgroundArt.animate().alpha(1f).setDuration(400).start();
        }
        return frame;
    }

    private LinearLayout.LayoutParams tileParams(int width, int leftMarginDp) {
        LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(width, -2);
        params.leftMargin = dp(leftMarginDp);
        return params;
    }

    private View gameTile(int index, int size) {
        boolean ready = installed(index);
        FrameLayout tile = new FrameLayout(this);
        tile.setId(2000 + index);
        ImageView cover = coverImage(index, !ready, 14);
        tile.addView(cover, new FrameLayout.LayoutParams(size, size));
        // Small tiles (landscape) leave little room: short label, tighter padding, never clipped.
        TextView state = label(getString(ready ? R.string.tile_ready : R.string.tile_add), size < dp(90) ? 10 : 11, UiKit.TEXT, true);
        state.setSingleLine(true);
        state.setPadding(dp(7), dp(3), dp(7), dp(3));
        state.setMaxWidth(size - dp(12));
        state.setEllipsize(android.text.TextUtils.TruncateAt.END);
        state.setBackground(UiKit.shape(this, 0xB3000000, 99));
        tile.addView(state, new FrameLayout.LayoutParams(-2, -2, ready ? Gravity.BOTTOM | Gravity.START : Gravity.CENTER));
        ((FrameLayout.LayoutParams) state.getLayoutParams()).setMargins(dp(6), dp(6), dp(6), dp(6));
        tile.setContentDescription(getString(ready ? R.string.tile_installed : R.string.tile_not_installed, InstallService.title(GAMES[index])));
        Gamepad.focusable(tile, 14);
        Gamepad.alsoOnFocus(tile, (v, has) -> {
            if (has) select(index);
            v.animate().translationY(has ? -dp(4) : 0).translationZ(has ? dp(8) : 0).setDuration(140).start();
        });
        Motion.pressable(tile);
        tile.setOnClickListener(v -> {
            if (selected != index) { select(index); return; }
            if (ready) playGame(GAMES[index]); else startAdd(index);
        });
        if (index == selected) tile.setFocusedByDefault(true);
        tiles[index] = tile;
        return tile;
    }

    private View utilityTile(String symbol, String text, int size, View.OnClickListener click) {
        LinearLayout tile = column();
        tile.setGravity(Gravity.CENTER);
        tile.setBackground(UiKit.shape(this, 0x14FFFFFF, 14));
        tile.setMinimumHeight(size);
        TextView icon = label(symbol, 22, UiKit.TEXT, false);
        icon.setGravity(Gravity.CENTER);
        tile.addView(icon);
        TextView name = label(text, 11, UiKit.MUTED, false);
        name.setGravity(Gravity.CENTER);
        tile.addView(name, topMargin(-2, 4));
        tile.setContentDescription(text);
        Gamepad.focusable(tile, 14);
        Motion.pressable(tile);
        tile.setOnClickListener(click);
        return tile;
    }

    private void select(int index) {
        if (index == selected) return;
        selected = index;
        getPreferences(0).edit().putInt("selected", index).apply();
        heroAnimate = true;
        fillHero();
        heroAnimate = false;
        root.post(this::refreshShelfHints);
    }

    private void refreshShelfHints() {
        if (screen != Screen.SHELF) return;
        View old = root.findViewWithTag("hints");
        if (old instanceof ViewGroup) ((ViewGroup) old.getParent()).removeView(old);
        ViewGroup frame = (ViewGroup) root.getChildAt(0);
        addHints(frame, Gamepad.Action.CONFIRM, getString(installed(selected) ? R.string.hint_play : R.string.hint_add),
                Gamepad.Action.OPTIONS, getString(R.string.hint_options), Gamepad.Action.PREV, "",
                Gamepad.Action.NEXT, getString(R.string.hint_switch_game));
    }

    /** Hero: state, title, last save or what is needed, and the two main actions. */
    private void fillHero() {
        if (heroBox == null) return;
        boolean wide = wide();
        String game = GAMES[selected];
        boolean ready = installed(selected);
        heroBox.removeAllViews();
        boolean animate = heroAnimate && Motion.enabled();
        if (animate) {
            crossfade(backgroundArt, COVERS[selected], 300);
            crossfade(heroArt, COVERS[selected], 300);
            heroArt.setScaleX(.94f);
            heroArt.setScaleY(.94f);
            heroArt.setRotation(-1.5f);
            heroArt.animate().scaleX(1f).scaleY(1f).rotation(0f).setDuration(300).setInterpolator(Motion.OUT).start();
        } else {
            backgroundArt.setImageResource(COVERS[selected]);
            heroArt.setImageResource(COVERS[selected]);
        }
        heroArt.setColorFilter(ready ? null : dimFilter());
        heroBox.addView(pill(getString(ready ? R.string.pill_installed : R.string.pill_not_installed), ready ? UiKit.ACCENT : UiKit.MUTED), wrap());
        heroBox.addView(heading(InstallService.title(game), wide ? 30 : 30, Gravity.START), topMargin(-1, wide ? 4 : 8));
        LinearLayout meta = new LinearLayout(this);
        if (ready) {
            GameSaves.Slot last = GameSaves.latest(this, game);
            if (last != null) {
                meta.addView(pill(UiKit.levelName(this, game, last.levelIndex) + " · " + Math.round(last.completionPercent) + "%", UiKit.TEXT));
                meta.addView(pill(DateUtils.getRelativeTimeSpanString(last.activeFile.lastModified()).toString(), UiKit.MUTED), leftMargin(8));
            } else {
                meta.addView(pill(getString(R.string.new_game), UiKit.TEXT));
            }
        } else {
            meta.addView(pill(getString(R.string.iso_size, String.format(Locale.ROOT, "%.1f", ISO_GB[selected])), UiKit.TEXT));
            meta.addView(pill(getString(R.string.needs_free, String.format(Locale.ROOT, "%.0f", Math.ceil(ISO_GB[selected] * 3 + 3))), UiKit.MUTED), leftMargin(8));
        }
        heroBox.addView(meta, topMargin(-2, wide ? 6 : 10));
        int stale = ready ? staleness(game) : LauncherStore.CURRENT;
        if (stale != LauncherStore.CURRENT) {
            // Tapping the warning explains it and offers to prepare the game again.
            TextView warning = pill(getString(stale == LauncherStore.INCOMPATIBLE ? R.string.stale_required_pill
                    : R.string.stale_recommended_pill), 0xFFFFC27A);
            warning.setOnClickListener(v -> explainStale(game));
            Gamepad.focusable(warning, 99);
            heroBox.addView(warning, topMargin(-2, 8));
        }
        Button main, second = null;
        if (ready && stale == LauncherStore.INCOMPATIBLE) {
            main = primary(getString(R.string.prepare_again), v -> startAdd(selected));
            second = ghost(getString(R.string.saves_and_options), v -> openOptions(selected));
        } else if (ready) {
            boolean hasSave = GameSaves.latest(this, game) != null;
            main = primary(getString(hasSave ? R.string.continue_game : R.string.play), v -> playGame(game));
            second = ghost(getString(R.string.saves_and_options), v -> openOptions(selected));
        } else {
            main = primary(getString(R.string.add_iso, InstallService.title(game)), v -> startAdd(selected));
        }
        main.setId(3000);
        if (second != null) second.setId(3001);
        heroBox.addView(second == null ? actions(true, main) : actions(true, main, second), topMargin(-1, wide ? 10 : 16));
        if (animate) Motion.slideIn(heroBox, 1, 40);
    }

    /** Fades from the current picture to the new one. */
    private void crossfade(ImageView view, int resource, int ms) {
        android.graphics.drawable.Drawable current = view.getDrawable();
        if (current instanceof android.graphics.drawable.TransitionDrawable)
            current = ((android.graphics.drawable.TransitionDrawable) current).getDrawable(1);
        android.graphics.drawable.Drawable next = getDrawable(resource);
        if (current == null || next == null) { view.setImageResource(resource); return; }
        android.graphics.drawable.TransitionDrawable fade =
                new android.graphics.drawable.TransitionDrawable(new android.graphics.drawable.Drawable[]{current, next});
        fade.setCrossFadeEnabled(true);
        view.setImageDrawable(fade);
        fade.startTransition(ms);
    }

    private void openOptions(int index) {
        if (!installed(index)) return;
        startActivity(new Intent(this, GameOptionsActivity.class).putExtra("game", GAMES[index]));
        overridePendingTransition(R.anim.options_enter, R.anim.shelf_exit);
    }

    /** Leaves touch mode and selects the screen's default element (gamepad/keyboard input). */
    private void focusDefault() {
        View target = findDefault(root);
        if (target != null) target.requestFocusFromTouch();
    }

    private static View findDefault(View view) {
        if (view.isFocusedByDefault() && view.isShown()) return view;
        if (view instanceof ViewGroup) {
            ViewGroup group = (ViewGroup) view;
            for (int i = 0; i < group.getChildCount(); i++) {
                View found = findDefault(group.getChildAt(i));
                if (found != null) return found;
            }
        }
        return null;
    }

    private void showSettings() {
        startActivityForResult(new Intent(this, SettingsActivity.class), SETTINGS);
        overridePendingTransition(R.anim.options_enter, R.anim.shelf_exit);
    }

    // ------------------------------------------------------------------ updates and game data

    private String toolsHash;

    /** The bundled toolchain's hash, which each installation records. */
    private String currentTools() {
        if (toolsHash != null) return toolsHash;
        try (InputStream input = getAssets().open("toolchain.sha256")) {
            ByteArrayOutputStream out = new ByteArrayOutputStream();
            InstallFiles.copy(input, out);
            toolsHash = out.toString("UTF-8").trim();
        } catch (IOException error) {
            toolsHash = "";
        }
        return toolsHash;
    }

    private int staleness(String game) {
        return LauncherStore.staleness(this, game, currentTools(), BuildConfig.GAME_DATA_VERSION);
    }

    private void explainStale(String game) {
        boolean required = staleness(game) == LauncherStore.INCOMPATIBLE;
        String title = InstallService.title(game);
        AlertDialog.Builder dialog = new AlertDialog.Builder(this)
                .setTitle(getString(R.string.prepare_again_title, title))
                .setMessage(getString(required ? R.string.stale_required_body : R.string.stale_recommended_body, title)
                        + "\n\n" + getString(R.string.data_is_kept))
                .setPositiveButton(R.string.prepare_again, (d, w) -> startAdd(gameIndex(game)))
                .setNegativeButton(R.string.not_now, null);
        if (required) dialog.setNeutralButton(R.string.play_anyway, (d, w) -> launchChecked(game));
        dialog.show();
    }

    private AlertDialog updatedDialog;

    /** Once after Precursor was updated: what changed for the installed games. */
    private void showUpdatedNotice() {
        int previous = Updater.lastRun(this);
        if (previous == BuildConfig.VERSION_CODE || isFinishing() || (updatedDialog != null && updatedDialog.isShowing())) return;
        if (previous == 0) { Updater.markRun(this); return; } // first run: nothing was updated
        List<String> required = new ArrayList<>(), recommended = new ArrayList<>();
        for (String game : GAMES) {
            if (LauncherStore.installed(this, game) == null) continue;
            int stale = staleness(game);
            if (stale == LauncherStore.INCOMPATIBLE) required.add(InstallService.title(game));
            else if (stale == LauncherStore.OUTDATED) recommended.add(InstallService.title(game));
        }
        String message;
        if (!required.isEmpty()) message = getString(R.string.updated_required, String.join(", ", required));
        else if (!recommended.isEmpty()) message = getString(R.string.updated_recommended, String.join(", ", recommended));
        else message = getString(R.string.updated_nothing);
        updatedDialog = new AlertDialog.Builder(this).setTitle(getString(R.string.updated_title, BuildConfig.VERSION_NAME))
                .setMessage(message + "\n\n" + getString(R.string.data_is_kept))
                // Recorded only once answered: turning the phone rebuilds the screen and shows it again.
                .setPositiveButton(R.string.ok, (d, w) -> Updater.markRun(this))
                .setOnCancelListener(d -> Updater.markRun(this)).show();
    }

    // ------------------------------------------------------------------ input

    @Override public boolean dispatchKeyEvent(KeyEvent event) {
        Gamepad.Action action = Gamepad.action(event);
        if (action == Gamepad.Action.NONE) return super.dispatchKeyEvent(event);
        if (event.getAction() != KeyEvent.ACTION_UP) return true;
        // Coming from touch: the first button only shows where the selection is.
        if (root.isInTouchMode() && action != Gamepad.Action.BACK && action != Gamepad.Action.SELECT
                && action != Gamepad.Action.PREV && action != Gamepad.Action.NEXT) {
            focusDefault();
            return true;
        }
        switch (action) {
            case CONFIRM: {
                View focused = getCurrentFocus();
                if (focused != null) focused.performClick();
                else { View first = root.findFocus(); if (first == null) root.requestFocus(View.FOCUS_DOWN); }
                return true;
            }
            case BACK: onBackPressed(); return true;
            case SELECT: askForReport(null); return true;
            default: break;
        }
        if (screen == Screen.SHELF) {
            switch (action) {
                case OPTIONS: openOptions(selected); return true;
                case ADD: if (!installed(selected)) startAdd(selected); return true;
                case PREV: case NEXT: {
                    int next = (selected + (action == Gamepad.Action.NEXT ? 1 : 2)) % 3;
                    select(next);
                    if (tiles[next] != null) tiles[next].requestFocusFromTouch();
                    return true;
                }
                case START: if (installed(selected)) playGame(GAMES[selected]); else startAdd(selected); return true;
                default: return true;
            }
        }
        if (action == Gamepad.Action.START) {
            View focused = getCurrentFocus();
            if (focused != null) focused.performClick();
        }
        return true;
    }

    @Override public void onBackPressed() {
        if (screen == Screen.WIZARD) {
            switch (wizardStep) {
                case W_PICK: goWizard(W_WELCOME); return;
                case W_ISO: if (wizardFirstRun) goWizard(W_PICK); else { screen = Screen.SHELF; rebuild(); } return;
                case W_CHECK: case W_PREPARE: moveTaskToBack(true); return;
                case W_DONE: leaveDone(); return;
                case W_ERROR: goWizard(W_ISO); return;
                default: break; // welcome: leave the app
            }
            if (installedCount() > 0 && !wizardFirstRun) { screen = Screen.SHELF; rebuild(); return; }
        }
        super.onBackPressed();
    }

    // ------------------------------------------------------------------ ISO and installation

    private void pickIsoFor(int game) {
        if (LauncherStore.installing(this)) { showError(getString(R.string.already_preparing_short)); return; }
        // Android 13+: without this the installation progress/cancel notification is hidden.
        if (Build.VERSION.SDK_INT >= 33
                && checkSelfPermission(android.Manifest.permission.POST_NOTIFICATIONS) != android.content.pm.PackageManager.PERMISSION_GRANTED
                && !getPreferences(0).getBoolean("askedNotifications", false)) {
            getPreferences(0).edit().putBoolean("askedNotifications", true).apply();
            requestPermissions(new String[]{android.Manifest.permission.POST_NOTIFICATIONS}, 5);
        }
        wizardGame = game;
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT).setType("*/*")
                .addCategory(Intent.CATEGORY_OPENABLE)
                .addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
        try { startActivityForResult(intent, PICK_ISO); }
        catch (ActivityNotFoundException error) { showError(getString(R.string.no_file_picker)); }
    }

    @Override protected void onActivityResult(int request, int result, Intent data) {
        super.onActivityResult(request, result, data);
        if (request == SETTINGS && result == SettingsActivity.RESULT_WELCOME) {
            screen = Screen.WIZARD; wizardFirstRun = true; wizardStep = W_WELCOME; rebuild();
            return;
        }
        if (result != RESULT_OK || data == null || data.getData() == null) return;
        Uri uri = data.getData();
        if (request == PICK_ISO) {
            if (LauncherStore.installing(this)) return;
            try {
                try { getContentResolver().takePersistableUriPermission(uri, Intent.FLAG_GRANT_READ_URI_PERMISSION); }
                catch (SecurityException ignored) { /* Transient grants still cover this job. */ }
                Intent install = new Intent(this, InstallService.class).setData(uri)
                        .putExtra("game", GAMES[wizardGame])
                        .addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
                install.setClipData(ClipData.newRawUri("ISO", uri));
                startForegroundService(install);
                startingUntil = SystemClock.elapsedRealtime() + 6000;
                getPreferences(0).edit().putString("pendingDone", GAMES[wizardGame]).apply();
                screen = Screen.WIZARD;
                wizardStep = W_CHECK;
                rebuild();
            } catch (Exception error) { showError(getString(R.string.could_not_start, error.getMessage())); }
        } else if (request == EXPORT_LOG) {
            new Thread(() -> {
                try (InputStream input = new FileInputStream(LauncherStore.log(this));
                     OutputStream output = getContentResolver().openOutputStream(uri, "wt")) {
                    if (output == null) throw new IOException(getString(R.string.cannot_open_destination));
                    InstallFiles.copy(input, output);
                    runOnUiThread(() -> Toast.makeText(this, R.string.log_saved, Toast.LENGTH_SHORT).show());
                } catch (Exception error) { runOnUiThread(() -> showError(error.getMessage())); }
            }, "Export log").start();
        }
    }

    private void showLog() {
        String text = readLogTail(48000);
        TextView view = label(text, 11, UiKit.TEXT, false);
        view.setTextIsSelectable(true);
        view.setTypeface(Typeface.MONOSPACE);
        view.setPadding(dp(16), dp(12), dp(16), dp(12));
        ScrollView scroll = new ScrollView(this);
        scroll.addView(view);
        new AlertDialog.Builder(this).setTitle(R.string.log_title).setView(scroll)
                .setPositiveButton(R.string.close, null).setNeutralButton(R.string.save_log, (dialog, which) -> {
                    if (!LauncherStore.log(this).isFile()) return;
                    Intent export = new Intent(Intent.ACTION_CREATE_DOCUMENT).setType("text/plain")
                            .addCategory(Intent.CATEGORY_OPENABLE).putExtra(Intent.EXTRA_TITLE, "precursor-install.log");
                    try { startActivityForResult(export, EXPORT_LOG); }
                    catch (ActivityNotFoundException error) { showError(getString(R.string.no_file_picker)); }
                }).show();
    }

    private String readLogTail(int limit) {
        try (RandomAccessFile file = new RandomAccessFile(LauncherStore.log(this), "r")) {
            long length = file.length();
            byte[] bytes = new byte[(int) Math.min(length, limit)];
            file.seek(length - bytes.length);
            file.readFully(bytes);
            return bytes.length == 0 ? getString(R.string.log_empty) :
                    new String(bytes, java.nio.charset.StandardCharsets.UTF_8);
        } catch (IOException error) { return getString(R.string.log_missing); }
    }

    // ------------------------------------------------------------------ playing

    /** Jak1Activity hands over a different game here after closing the one that was running. */
    private void handleLaunchRequest(Intent intent) {
        String game = intent == null ? null : intent.getStringExtra(EXTRA_LAUNCH_GAME);
        if (game == null) return;
        intent.removeExtra(EXTRA_LAUNCH_GAME);
        if (InstallFiles.validGame(game) && LauncherStore.installed(this, game) != null)
            handler.postDelayed(() -> startGameWhenClosed(game, 0), 300);
    }

    /** Pid of the running game process, or -1. */
    private int runningGamePid() {
        List<ActivityManager.RunningAppProcessInfo> processes =
                getSystemService(ActivityManager.class).getRunningAppProcesses();
        if (processes != null) {
            for (ActivityManager.RunningAppProcessInfo process : processes)
                if ((getPackageName() + ":game").equals(process.processName)) return process.pid;
        }
        return -1;
    }

    /** Checks the device and the running session before starting a game. */
    private void playGame(String game) {
        if (installed(gameIndex(game)) && staleness(game) == LauncherStore.INCOMPATIBLE) { explainStale(game); return; }
        launchChecked(game);
    }

    /** playGame without the "prepare again" check (the player chose to play anyway). */
    private void launchChecked(String game) {
        int gles = getSystemService(ActivityManager.class).getDeviceConfigurationInfo().reqGlEsVersion;
        if (gles < GLES_3_2 && !getPreferences(0).getBoolean("ignoreGles", false)) {
            new AlertDialog.Builder(this).setTitle(R.string.unsupported_title)
                    .setMessage(getString(R.string.unsupported_body, (gles >> 16) + "." + (gles & 0xffff)))
                    .setNegativeButton(R.string.cancel, null)
                    .setPositiveButton(R.string.try_anyway, (d, w) -> {
                        getPreferences(0).edit().putBoolean("ignoreGles", true).apply();
                        launchChecked(game);
                    }).show();
            return;
        }
        int pid = runningGamePid();
        String running = LauncherStore.read(LauncherStore.session(this)).optString("game");
        if (pid > 0 && running.equals(game)) {
            startActivity(new Intent(this, Jak1Activity.class).putExtra("game", game));
            return;
        }
        if (pid > 0) {
            String open = InstallFiles.validGame(running) ? InstallService.title(running) : getString(R.string.another_game);
            new AlertDialog.Builder(this).setTitle(getString(R.string.still_open_title, open))
                    .setMessage(getString(R.string.still_open_body, InstallService.title(game), open))
                    .setNegativeButton(R.string.cancel, null)
                    .setPositiveButton(R.string.close_and_open, (d, w) -> {
                        new android.util.AtomicFile(LauncherStore.session(this)).delete();
                        android.os.Process.killProcess(pid);
                        startGameWhenClosed(game, 0);
                    }).show();
            return;
        }
        launchFresh(game);
    }

    /**
     * Jak1Activity is singleInstance: after its process dies Android keeps the task and would
     * recreate the activity from the OLD intent (the previous game) and pass the new one to
     * onNewIntent. Clearing the task guarantees the activity starts with the requested game.
     */
    private void launchFresh(String game) {
        if (transitioning) return;
        Runnable start = () -> {
            launchedGame = true;
            startActivity(new Intent(this, Jak1Activity.class).putExtra("game", game)
                            .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TASK),
                    ActivityOptions.makeCustomAnimation(this, R.anim.game_enter, R.anim.hold).toBundle());
        };
        if (screen != Screen.SHELF || heroArt == null || !heroArt.isShown() || heroArt.getWidth() == 0 || !Motion.enabled()) {
            start.run();
            return;
        }
        transitioning = true;
        zoomCover(start);
    }

    /** The hero cover grows to fill the screen and fades to black (about 0.6 s). */
    private void zoomCover(Runnable then) {
        int[] rootAt = new int[2], artAt = new int[2];
        root.getLocationInWindow(rootAt);
        heroArt.getLocationInWindow(artAt);
        int x0 = artAt[0] - rootAt[0], y0 = artAt[1] - rootAt[1];
        int w0 = heroArt.getWidth(), h0 = heroArt.getHeight(), w1 = root.getWidth(), h1 = root.getHeight();
        ImageView zoom = new ImageView(this);
        zoom.setImageResource(COVERS[selected]);
        zoom.setScaleType(ImageView.ScaleType.CENTER_CROP);
        zoom.setBackground(UiKit.shape(this, UiKit.PANEL, 18));
        zoom.setClipToOutline(true);
        zoom.setElevation(dp(16));
        FrameLayout.LayoutParams params = new FrameLayout.LayoutParams(w0, h0);
        params.leftMargin = x0;
        params.topMargin = y0;
        root.addView(zoom, params);
        View black = new View(this);
        black.setBackgroundColor(0xFF000000);
        black.setAlpha(0f);
        black.setElevation(dp(17));
        root.addView(black, new FrameLayout.LayoutParams(-1, -1));
        ValueAnimator grow = ValueAnimator.ofFloat(0f, 1f).setDuration(420);
        grow.setInterpolator(new android.view.animation.PathInterpolator(.2f, 0f, 0f, 1f));
        grow.addUpdateListener(a -> {
            float t = (float) a.getAnimatedValue();
            params.leftMargin = Math.round(x0 * (1 - t));
            params.topMargin = Math.round(y0 * (1 - t));
            params.width = Math.round(w0 + (w1 - w0) * t);
            params.height = Math.round(h0 + (h1 - h0) * t);
            zoom.setLayoutParams(params);
        });
        grow.start();
        black.animate().alpha(1f).setStartDelay(260).setDuration(300).withEndAction(then).start();
    }

    private void startGameWhenClosed(String game, int attempt) {
        if (runningGamePid() > 0 && attempt < 30) {
            handler.postDelayed(() -> startGameWhenClosed(game, attempt + 1), 100);
            return;
        }
        // Keep a dump the previous session may have left before the new one truncates it.
        checkLastSession();
        launchFresh(game);
    }

    /** If the last game session ended abnormally, keep its evidence and offer to send a report. */
    private boolean checkLastSession() {
        File marker = LauncherStore.session(this);
        if (!marker.exists() || runningGamePid() > 0) return false;
        JSONObject session = LauncherStore.read(marker);
        new android.util.AtomicFile(marker).delete();
        String what = ReportBuilder.captureAbnormalSession(this, session);
        if (what == null || crashPromptShown) return false;
        crashPromptShown = true;
        new AlertDialog.Builder(this).setTitle(R.string.crash_title)
                .setMessage(getString(R.string.crash_body, what))
                .setNegativeButton(R.string.not_now, (d, w) -> crashPromptShown = false)
                .setPositiveButton(R.string.send_report, (d, w) -> { crashPromptShown = false; askForReport(what); })
                .show();
        return true;
    }

    private void askForReport(String headline) {
        EditText note = new EditText(this);
        note.setHint(R.string.report_hint);
        note.setMinLines(3);
        note.setGravity(Gravity.TOP);
        if (headline != null) note.setText(headline + ". ");
        LinearLayout box = column();
        box.setPadding(dp(20), dp(8), dp(20), 0);
        box.addView(note);
        new AlertDialog.Builder(this).setTitle(R.string.report_title)
                .setMessage(R.string.report_body)
                .setView(box)
                .setNegativeButton(R.string.cancel, null)
                .setPositiveButton(R.string.create_and_share, (d, w) -> shareReport(note.getText().toString()))
                .show();
    }

    private void shareReport(String note) {
        Toast.makeText(this, R.string.preparing_report, Toast.LENGTH_SHORT).show();
        new Thread(() -> {
            try {
                File report = ReportBuilder.build(this, note);
                Uri uri = ReportProvider.uriFor(this, report);
                Intent send = new Intent(Intent.ACTION_SEND).setType("application/zip")
                        .putExtra(Intent.EXTRA_STREAM, uri)
                        .putExtra(Intent.EXTRA_SUBJECT, getString(R.string.report_subject, BuildConfig.GIT_COMMIT))
                        .putExtra(Intent.EXTRA_TEXT, note)
                        .addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
                send.setClipData(ClipData.newRawUri("reporte", uri));
                runOnUiThread(() -> {
                    ReportBuilder.clearPending(this);
                    startActivity(Intent.createChooser(send, getString(R.string.send_report)));
                });
            } catch (Exception error) {
                runOnUiThread(() -> showError(getString(R.string.report_failed, error.getMessage())));
            }
        }, "Build report").start();
    }

    private void showError(String text) {
        new AlertDialog.Builder(this).setTitle(R.string.app_name).setMessage(text).setPositiveButton(R.string.ok, null).show();
    }

    // ------------------------------------------------------------------ small view helpers

    private boolean wide() { return getResources().getConfiguration().orientation == Configuration.ORIENTATION_LANDSCAPE; }
    private int screenWidth() { return getResources().getDisplayMetrics().widthPixels; }
    private int screenHeight() { return getResources().getDisplayMetrics().heightPixels; }
    private int dp(int value) { return UiKit.dp(this, value); }
    private LinearLayout column() { return UiKit.column(this); }
    private TextView label(String text, int size, int color, boolean bold) { return UiKit.label(this, text, size, color, bold); }
    private static int clampGame(int index) { return Math.max(0, Math.min(2, index)); }
    private static int gameIndex(String game) { int i = Arrays.asList(GAMES).indexOf(game); return i < 0 ? 0 : i; }
    private boolean installed(int index) { return LauncherStore.installed(this, GAMES[index]) != null; }
    private int installedCount() { int n = 0; for (int i = 0; i < 3; i++) if (installed(i)) n++; return n; }

    private LinearLayout.LayoutParams topMargin(int width, int top) {
        LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(width, -2);
        params.topMargin = dp(top);
        return params;
    }

    private static LinearLayout.LayoutParams wrap() { return new LinearLayout.LayoutParams(-2, -2); }

    private LinearLayout.LayoutParams leftMargin(int left) {
        LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(-2, -2);
        params.leftMargin = dp(left);
        return params;
    }

    private TextView heading(String text, int size, int gravity) {
        TextView view = label(text, size, UiKit.TEXT, true);
        view.setTypeface(Typeface.create("sans-serif-condensed", Typeface.BOLD));
        view.setGravity(gravity);
        view.setLineSpacing(0, .95f);
        return view;
    }

    private TextView body(String text, int gravity) {
        TextView view = label(text, 15, 0xFFC3CFD8, false);
        view.setGravity(gravity);
        view.setLineSpacing(dp(2), 1f);
        return view;
    }

    private TextView pill(String text, int color) {
        TextView view = label(text, 12, color, true);
        view.setPadding(dp(10), dp(4), dp(10), dp(4));
        view.setBackground(UiKit.shape(this, 0x1FFFFFFF, 99));
        return view;
    }

    private Button primary(String text, View.OnClickListener click) {
        Button button = UiKit.button(this, text, UiKit.ACCENT, UiKit.BACKGROUND);
        button.setBackground(UiKit.shape(this, UiKit.ACCENT, 99));
        button.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
        button.setTextSize(16);
        button.setPadding(dp(22), dp(10), dp(22), dp(10));
        button.setOnClickListener(click);
        return Motion.pressable(Gamepad.focusable(button, 99));
    }

    private Button ghost(String text, View.OnClickListener click) {
        Button button = UiKit.button(this, text, 0x26FFFFFF, UiKit.TEXT);
        button.setBackground(UiKit.shape(this, 0x26FFFFFF, 99));
        button.setTextSize(15);
        button.setPadding(dp(18), dp(10), dp(18), dp(10));
        button.setOnClickListener(click);
        return Motion.pressable(Gamepad.focusable(button, 99));
    }

    /** Buttons in a row (landscape) or stacked full width (portrait). */
    private LinearLayout actions(boolean wide, Button... buttons) {
        LinearLayout box = new LinearLayout(this);
        box.setOrientation(wide ? LinearLayout.HORIZONTAL : LinearLayout.VERTICAL);
        for (int i = 0; i < buttons.length; i++) {
            LinearLayout.LayoutParams params = wide ? new LinearLayout.LayoutParams(-2, -2) : new LinearLayout.LayoutParams(-1, -2);
            if (i > 0) { if (wide) params.leftMargin = dp(12); else params.topMargin = dp(10); }
            box.addView(buttons[i], params);
        }
        return box;
    }

    private View dots() {
        LinearLayout dots = new LinearLayout(this);
        int current = Math.min(wizardStep, W_DONE);
        int previous = lastDotsStep < 0 ? current : lastDotsStep;
        lastDotsStep = current;
        for (int i = 1; i <= 5; i++) {
            View dot = new View(this);
            int color = i == current ? UiKit.ACCENT : i < current ? 0x804EE878 : 0x2EFFFFFF;
            dot.setBackground(UiKit.shape(this, color, 99));
            LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(dp(i == previous ? 18 : 8), dp(8));
            if (i > 1) params.leftMargin = dp(6);
            dots.addView(dot, params);
            int to = dp(i == current ? 18 : 8);
            if (params.width != to) {
                ValueAnimator stretch = ValueAnimator.ofInt(params.width, to).setDuration(220);
                stretch.setInterpolator(Motion.OUT);
                stretch.addUpdateListener(a -> { params.width = (int) a.getAnimatedValue(); dot.requestLayout(); });
                stretch.setStartDelay(80);
                stretch.start();
            }
        }
        dots.setContentDescription(getString(R.string.step_of, current));
        return dots;
    }

    private GradientDrawable leftRule() {
        GradientDrawable rule = new GradientDrawable(GradientDrawable.Orientation.LEFT_RIGHT,
                new int[]{0xFFE2B25A, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000});
        return rule;
    }

    /** "PRECURSOR · powered by OpenGOAL · no oficial" with the Orbe. */
    private View lockup(int sizeSp) {
        LinearLayout row = new LinearLayout(this);
        row.setGravity(Gravity.CENTER_VERTICAL);
        OrbView orb = new OrbView(this, OrbView.Mode.STATIC);
        int s = Math.round(sizeSp * 2.1f * getResources().getDisplayMetrics().scaledDensity);
        row.addView(orb, new LinearLayout.LayoutParams(s, s));
        LinearLayout text = column();
        TextView brand = label("PRECURSOR", sizeSp, UiKit.TEXT, true);
        brand.setTypeface(Typeface.create("sans-serif-condensed", Typeface.BOLD));
        brand.setLetterSpacing(.04f);
        text.addView(brand);
        TextView sub = label(getString(R.string.brand_tagline), Math.max(9, sizeSp * 45 / 100), UiKit.MUTED, false);
        sub.setLetterSpacing(.1f);
        sub.setSingleLine(true);
        text.addView(sub);
        LinearLayout.LayoutParams textParams = new LinearLayout.LayoutParams(-2, -2);
        textParams.leftMargin = dp(10);
        row.addView(text, textParams);
        return row;
    }

    private ImageView coverImage(int index, boolean dim, int radius) {
        ImageView image = new ImageView(this);
        image.setImageResource(COVERS[index]);
        image.setScaleType(ImageView.ScaleType.CENTER_CROP);
        image.setBackground(UiKit.shape(this, UiKit.PANEL, radius));
        image.setClipToOutline(true);
        if (dim) image.setColorFilter(dimFilter());
        return image;
    }

    private static ColorMatrixColorFilter dimFilter() {
        ColorMatrix gray = new ColorMatrix();
        gray.setSaturation(.15f);
        ColorMatrix dark = new ColorMatrix(new float[]{
                .5f, 0, 0, 0, 0, 0, .5f, 0, 0, 0, 0, 0, .5f, 0, 0, 0, 0, 0, 1, 0});
        gray.postConcat(dark);
        return new ColorMatrixColorFilter(gray);
    }

    private void addHints(ViewGroup frame, Object... pairs) {
        View hints = Gamepad.hintBar(this, pairs);
        if (hints == null) return;
        hints.setTag("hints");
        hints.setPadding(dp(16), dp(6), dp(20), dp(10));
        frame.addView(hints, new FrameLayout.LayoutParams(-1, -2, Gravity.BOTTOM));
    }

    /** Dialogs and the edge swipe can bring the bars back; hide them again on return. (AI-assisted) */
    @Override public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) UiKit.immersive(this);
    }
}
