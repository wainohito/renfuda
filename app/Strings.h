/*
 * BDB card database - RenkuOS application
 * All user interface strings in one place (UI spec v0.3, 1, 14.1).
 *
 * Every string has a Japanese and an English text. The language is chosen
 * once at start-up (Japanese when the system's first preferred language is
 * Japanese or unknown, English otherwise) and does not change while the
 * application runs. A str::Text converts to const char* in the chosen
 * language, so it can be used wherever a C string is expected (but use
 * str::kX.Get() for printf style varargs). A later step can move the
 * texts into Locale Kit catalogs.
 */
#ifndef BDB_STRINGS_H
#define BDB_STRINGS_H


namespace str {


// True when the UI is shown in English.
extern bool gEnglish;
// "ja" or "en" (logs, presets).
inline const char* Language() { return gEnglish ? "en" : "ja"; }
// Decides the language from the system settings (call once at start-up).
void InitLanguage();


struct Text {
	const char*	ja;
	const char*	en;

	const char*	Get() const { return gEnglish ? en : ja; }
	operator const char*() const { return Get(); }
};


// Application
static const Text kAppName = { "連札", "Renfuda" };
static const Text kAboutText = {
	"連札(Renfuda) カード型データベース\n試作版 %s\n\nRenkuOS 用\n\n"
	"アイコン: haiku-icons(Haiku の data/artwork を書き出したもの)\n"
	"https://github.com/darealshinji/haiku-icons\n"
	"Copyright (c) 2007-2026 Haiku, Inc. MIT ライセンス\n"
	"(Haiku のロゴと葉のマークは同梱していません)",
	"Renfuda card database\nPrototype %s\n\nFor RenkuOS\n\n"
	"Icons: haiku-icons (exported from Haiku's data/artwork)\n"
	"https://github.com/darealshinji/haiku-icons\n"
	"Copyright (c) 2007-2026 Haiku, Inc. MIT license\n"
	"(The Haiku logo and leaf marks are not included)" };

// Menus
static const Text kMenuFile = { "ファイル", "File" };
static const Text kMenuEdit = { "編集", "Edit" };
static const Text kMenuCard = { "カード", "Card" };
static const Text kMenuMode = { "モード", "Mode" };
static const Text kMenuView = { "表示", "View" };
static const Text kMenuStack = { "スタック", "Stack" };
static const Text kMenuHelp = { "ヘルプ", "Help" };
static const Text kMenuPart = { "部品", "Part" };
static const Text kMenuTemplate = { "テンプレート", "Template" };
static const Text kMenuFind = { "検索", "Find" };

static const Text kNewStack = { "新規スタック…", "New stack…" };
static const Text kNewStackFromCsv = { "CSVから新規スタックを作る…",
	"New stack from CSV…" };
static const Text kOpenStack = { "スタックを開く…", "Open stack…" };
static const Text kRecentStacks = { "最近使ったスタック", "Recent stacks" };
static const Text kSaveCardPdf = { "現在のカードをPDFで保存…",
	"Save current card as PDF…" };
static const Text kSaveSelectedPdf = { "選択したカードをPDFで保存…",
	"Save selected cards as PDF…" };
static const Text kSaveListPdf = { "一覧をPDFで保存…", "Save list as PDF…" };
static const Text kSaveAllPdf = { "すべてのカードをPDFで保存…",
	"Save all cards as PDF…" };
static const Text kClose = { "閉じる", "Close" };
static const Text kQuit = { "終了", "Quit" };

static const Text kUndo = { "取り消し", "Undo" };
static const Text kRedo = { "やり直し", "Redo" };
static const Text kCut = { "切り取り", "Cut" };
static const Text kCopy = { "コピー", "Copy" };
static const Text kPaste = { "貼り付け", "Paste" };
static const Text kSelectAll = { "すべて選択", "Select all" };
static const Text kDuplicatePart = { "部品の複製", "Duplicate part" };
static const Text kDeletePart = { "部品の削除", "Delete part" };

static const Text kBringToFront = { "前面へ", "Bring to front" };
static const Text kSendToBack = { "背面へ", "Send to back" };
static const Text kLock = { "ロック", "Lock" };
static const Text kUnlock = { "ロック解除", "Unlock" };
static const Text kLockAll = { "すべてをロック", "Lock all" };
static const Text kUnlockAll = { "すべてのロックを解除", "Unlock all" };
static const Text kChooseIcon = { "アイコンを選ぶ…", "Choose icon…" };
static const Text kSetLink = { "リンクを設定…", "Set link…" };
static const Text kKindSubmenu = { "型", "Type" };

static const Text kTemplateProperties = { "テンプレートのプロパティ…",
	"Template properties…" };
static const Text kPaperMenu = { "用紙サイズと向き…", "Paper size…" };
static const Text kBackgroundMenu = { "背景を設定…", "Background…" };
static const Text kThemeMenu = { "テーマを選ぶ…", "Theme…" };

static const Text kNewCard = { "新規カード", "New card" };
static const Text kDuplicateCard = { "カードを複製", "Duplicate card" };
static const Text kDeleteCard = { "カードを削除…", "Delete card…" };
static const Text kPrevCard = { "前のカード", "Previous card" };
static const Text kNextCard = { "次のカード", "Next card" };
static const Text kFirstCard = { "最初のカード", "First card" };
static const Text kLastCard = { "最後のカード", "Last card" };
static const Text kGoToNumber = { "カード番号へ移動…", "Go to card…" };
static const Text kChangeTemplate = { "テンプレートを変更",
	"Change template" };
static const Text kShowAll = { "すべて表示", "Show all" };
static const Text kRunFind = { "検索を実行", "Find" };
static const Text kClearFind = { "条件をクリア", "Clear conditions" };

static const Text kModePlay = { "プレイモード", "Play mode" };
static const Text kModeEdit = { "編集モード", "Edit mode" };
static const Text kModeFind = { "検索モード", "Find mode" };
// The mode buttons of the status bar.
static const Text kModeButtonPlay = { "プレイ", "Play" };
static const Text kModeButtonFind = { "検索", "Find" };
static const Text kModeButtonEdit = { "編集", "Edit" };
static const Text kZoomTip = { "表示の倍率", "Zoom" };

static const Text kShowList = { "一覧を表示", "Show list" };
static const Text kZoomIn = { "拡大", "Zoom in" };
static const Text kZoomOut = { "縮小", "Zoom out" };
static const Text kZoomActual = { "等倍", "Actual size" };
static const Text kZoomFit = { "ウィンドウに合わせる", "Fit to window" };
static const Text kShowGrid = { "グリッドを表示", "Show grid" };
static const Text kSnapToGrid = { "グリッドにスナップ", "Snap to grid" };

static const Text kImportCsv = { "CSVを読み込む…", "Import CSV…" };
static const Text kFetchImages = { "画像を取得…", "Fetch images…" };
static const Text kCleanup = { "スタックを整理…", "Clean up stack…" };
static const Text kProblems = { "問題リスト…", "Problems…" };
static const Text kManageTemplates = { "テンプレートの管理…",
	"Manage templates…" };
static const Text kStackSettings = { "スタックの設定…", "Stack settings…" };
static const Text kOpenLog = { "ログを開く", "Open log" };

static const Text kAbout = { "このアプリについて", "About Renfuda" };

// Buttons
static const Text kOK = { "OK", "OK" };
static const Text kCancel = { "キャンセル", "Cancel" };
static const Text kDelete = { "削除", "Delete" };
static const Text kApply = { "適用", "Apply" };
static const Text kCreate = { "作成…", "Create…" };
static const Text kFind = { "検索", "Find" };
static const Text kContinue = { "続ける", "Continue" };
static const Text kOverwrite = { "上書きする", "Overwrite" };
static const Text kReload = { "読み直す", "Reload" };
static const Text kConvert = { "変換する", "Convert" };
static const Text kKeepValues = { "値は変換せず保持する", "Keep the values" };
static const Text kDoCleanup = { "整理する", "Clean up" };
static const Text kCloseWithoutSaving = { "保存せずに閉じる",
	"Close without saving" };
static const Text kRetry = { "再試行", "Retry" };
static const Text kBack = { "戻る", "Back" };
static const Text kNext = { "次へ", "Next" };
static const Text kDone = { "閉じる", "Close" };
static const Text kChoose = { "選ぶ…", "Choose…" };
static const Text kChooseShort = { "選ぶ", "Choose" };
static const Text kClear = { "クリア", "Clear" };
static const Text kOpen = { "開く", "Open" };
static const Text kGo = { "移動", "Go" };
static const Text kSave = { "保存", "Save" };

// Start window
static const Text kStartTitle = { "連札", "Renfuda" };
static const Text kStartBlank = { "真っさらなカードから始める",
	"Start with a blank card" };
static const Text kStartPreset = { "テンプレートから始める",
	"Start from a template" };
static const Text kStartCsv = { "CSVから始める…", "Start from CSV…" };
static const Text kStartOpen = { "スタックを開く…", "Open a stack…" };
static const Text kStartRecent = { "最近使ったスタック", "Recent stacks" };
static const Text kSampleMark = { "サンプル", "Sample" };
static const Text kRemoveFromList = { "一覧から外す", "Remove from list" };
static const Text kPresetTitle = { "テンプレートから始める",
	"Start from a template" };
static const Text kPresetNone = { "テンプレートが見つかりません。",
	"No templates were found." };
static const Text kSavePanelTitle = { "新しいスタックの保存先",
	"Where to save the new stack" };
static const Text kNewStackName = { "新しいスタック", "New stack" };
static const Text kSampleCopyFailed = {
	"サンプルをコピーできませんでした(%s)。",
	"The sample could not be copied (%s)." };
static const Text kFetchSampleQuestion = {
	"このスタックの画像をネットから取得しますか?\n"
	"画像の取得元の利用規約と著作権は、ご自身で確認してください。",
	"Fetch the images of this stack from the network?\n"
	"Please check the terms of use and copyright of the image source." };
static const Text kFetchNow = { "取得する", "Fetch" };
static const Text kFetchLater = { "あとで", "Later" };

// Paper (UI 3.4)
static const Text kPaperTitle = { "用紙サイズと向き", "Paper size" };
static const Text kPaperSize = { "用紙サイズ", "Paper" };
static const Text kPaperOrientation = { "向き", "Orientation" };
static const Text kPaperScreen = { "画面用の標準(512×342)",
	"Screen standard (512×342)" };
static const Text kPaperPostcard = { "ハガキ", "Postcard" };
static const Text kPaperCustom = { "自由な大きさ", "Custom size" };
static const Text kPortrait = { "縦", "Portrait" };
static const Text kLandscape = { "横", "Landscape" };
static const Text kPaperWidth = { "幅", "Width" };
static const Text kPaperHeight = { "高さ", "Height" };
static const Text kPaperOutside = {
	"カード面の外にはみ出す部品が %d 個あります。",
	"%d parts are outside the card." };

// Status bar and titles
static const Text kStatusPosition = { "カード %d / %d", "Card %d / %d" };
static const Text kStatusTemplate = { "テンプレート: %s", "Template: %s" };
static const Text kStatusFound = { "検索結果 %d / %d", "Found %d / %d" };
static const Text kStatusProblems = { "問題が %d 件あります",
	"%d problems" };
static const Text kStatusSaveFailed = { "保存できませんでした",
	"Could not save" };
static const Text kStatusNoCards = { "カードがありません", "No cards" };
static const Text kStatusNewCard = { "新しいカード(入力すると保存されます)",
	"New card (saved when you type)" };
static const Text kStatusFetching = { "画像を取得中 %d / %d",
	"Fetching images %d / %d" };
static const Text kStatusFetchDone = {
	"画像の取得: 成功 %d 件、失敗 %d 件",
	"Images: %d fetched, %d failed" };
static const Text kWindowTitleEdit = { "%s — 編集モード: %s",
	"%s — Edit mode: %s" };
static const Text kWindowTitleFind = { "%s — 検索モード", "%s — Find mode" };
static const Text kBannerEdit = {
	"テンプレートを編集中: %s — このテンプレートを使う %d 枚のカードに"
	"反映されます",
	"Editing the template: %s — changes apply to the %d cards using it" };
static const Text kBannerFind = {
	"検索中 — 条件を入力して Enter キーを押してください",
	"Find — enter conditions and press Enter" };
static const Text kEndEdit = { "編集を終える", "Done editing" };

// Part kinds as shown to the user
static const Text kKindBlank = { "未設定", "Unset" };
static const Text kKindText = { "テキスト", "Text" };
static const Text kKindNumber = { "数字", "Number" };
static const Text kKindBool = { "スイッチ", "Switch" };
static const Text kKindImage = { "画像", "Image" };
static const Text kKindLabel = { "ラベル", "Label" };
static const Text kKindButton = { "ボタン", "Button" };
static const Text kKindCounter = { "位置の表示", "Position" };
static const Text kKindUnknown = { "未対応の部品", "Unsupported part" };

// Palette (UI 6.2)
static const Text kToolsTitle = { "部品", "Parts" };
static const Text kToolSelect = { "選択", "Select" };
static const Text kPaletteBlank = { "枠(未設定)", "Frame (unset)" };
static const Text kPaletteText = { "テキスト枠", "Text field" };
static const Text kPaletteNumber = { "数字枠", "Number field" };
static const Text kPaletteBool = { "スイッチ", "Switch" };
static const Text kPaletteImage = { "画像枠", "Image" };
static const Text kPaletteLabel = { "ラベル", "Label" };
static const Text kPaletteButton = { "ボタン", "Button" };
static const Text kPaletteFirst = { "最初へ", "First" };
static const Text kPalettePrev = { "前へ", "Previous" };
static const Text kPaletteNext = { "次へ", "Next" };
static const Text kPaletteLast = { "最後へ", "Last" };
static const Text kPaletteNavFull = { "ナビゲーションの組(最初・最後つき)",
	"Navigation (with first / last)" };
static const Text kPaletteCounter = { "位置の表示", "Position" };
static const Text kPaletteNav = { "ナビゲーションの組", "Navigation" };
// Default names: "テキスト1" / "Text 1"
static const Text kDefaultNameFormat = { "%s%d", "%s %d" };
static const Text kNewLabelText = { "ラベル", "Label" };
static const Text kNewButtonText = { "ボタン", "Button" };

// Inspector (UI 6.5, 6.12-6.15)
static const Text kInspectorTitle = { "インスペクタ", "Inspector" };
static const Text kInspectorMulti = { "%d 個の部品", "%d parts" };
static const Text kInspectorTemplate = { "テンプレート", "Template" };
static const Text kFieldName = { "名前", "Name" };
static const Text kFieldKind = { "型", "Type" };
static const Text kFieldX = { "X", "X" };
static const Text kFieldY = { "Y", "Y" };
static const Text kFieldW = { "幅", "Width" };
static const Text kFieldH = { "高さ", "Height" };
static const Text kFieldDefault = { "初期値", "Default" };
static const Text kFieldMultiline = { "複数行", "Multiple lines" };
static const Text kFieldDecimals = { "小数の桁数", "Decimals" };
static const Text kFieldFit = { "はめ方", "Fit" };
static const Text kFieldLabelText = { "文字列", "Text" };
static const Text kFieldAction = { "動作", "Action" };
static const Text kFieldBoolLook = { "見た目", "Look" };
static const Text kBoolOn = { "オン", "On" };
static const Text kBoolOff = { "オフ", "Off" };
static const Text kBoolNone = { "なし", "None" };
static const Text kBoolAny = { "指定なし", "Any" };
static const Text kLookSwitch = { "スイッチ", "Switch" };
static const Text kLookCheckbox = { "チェックボックス", "Check box" };
static const Text kFitContain = { "収める", "Contain" };
static const Text kFitCover = { "全体に合わせる", "Cover" };
static const Text kFitStretch = { "引き伸ばす", "Stretch" };
static const Text kFitTile = { "並べる", "Tile" };
static const Text kNameDuplicate = { "同じ名前の項目があります",
	"Another part has this name" };
static const Text kLockedNote = { "ロック中", "Locked" };
static const Text kSectionStyle = { "スタイル", "Style" };
static const Text kStyleBorder = { "枠線", "Border" };
static const Text kStyleBorderShow = { "表示する", "Show" };
static const Text kStyleWidth = { "太さ", "Width" };
static const Text kStyleColor = { "色", "Color" };
static const Text kStyleFill = { "枠内の背景", "Fill" };
static const Text kStyleTransparent = { "透明", "Transparent" };
static const Text kStyleFont = { "フォント", "Font" };
static const Text kStyleSize = { "サイズ", "Size" };
static const Text kStyleTextColor = { "文字の色", "Text color" };
static const Text kStyleSystemFont = { "システムの標準", "System font" };
static const Text kStyleReset = { "テーマの値に戻す", "Use theme values" };
static const Text kSectionIcon = { "アイコン", "Icon" };
static const Text kIconSize = { "大きさ", "Size" };
static const Text kIconPlacement = { "位置", "Position" };
static const Text kIconLeft = { "左", "Left" };
static const Text kIconTop = { "上", "Top" };
static const Text kSectionLink = { "リンク", "Link" };
static const Text kLinkNone = { "なし", "None" };
static const Text kLinkUrl = { "固定のURL", "Fixed URL" };
static const Text kLinkField = { "項目から", "From a field" };
static const Text kSectionSource = { "画像の取得元", "Image source" };
static const Text kSourceField = { "取得元の項目", "Source field" };
static const Text kSourceNone = { "(なし)", "(none)" };
static const Text kNoTemplates = { "(なし)", "(none)" };
static const Text kSourceEdge = { "サムネイルの大きさ", "Thumbnail size" };
static const Text kTemplateBackground = { "背景色", "Background color" };
static const Text kTemplateSystemColor = { "システムの色に戻す",
	"Use system color" };
static const Text kTemplateImage = { "背景画像", "Background image" };
static const Text kTemplateChooseFile = { "ファイルを選ぶ…", "Choose file…" };
static const Text kTemplateTheme = { "テーマ", "Theme" };
static const Text kTemplateApplyAll = {
	"このスタックのすべてのテンプレートに適用",
	"Apply to all templates of this stack" };
static const Text kThemeOverwrite = {
	"このスタックには、別の版のテーマ「%s」があります。上書きしますか?",
	"This stack has another revision of the theme \"%s\". Replace it?" };
static const Text kColorTitle = { "色を選ぶ", "Choose a color" };
static const Text kRecentColors = { "最近使った色", "Recent colors" };

// Button actions (data spec 7)
static const Text kActionNone = { "(なし)", "(none)" };
static const Text kActionFirst = { "最初のカードへ", "First card" };
static const Text kActionPrev = { "前のカードへ", "Previous card" };
static const Text kActionNext = { "次のカードへ", "Next card" };
static const Text kActionLast = { "最後のカードへ", "Last card" };
static const Text kActionNew = { "カードを追加", "New card" };
static const Text kActionDuplicate = { "カードを複製", "Duplicate card" };
static const Text kActionDelete = { "カードを削除", "Delete card" };
static const Text kActionList = { "一覧を開く", "Open list" };
static const Text kActionLink = { "リンクを開く", "Open link" };
static const Text kActionFindStart = { "検索モードに入る", "Start find" };
static const Text kActionFindRun = { "検索を実行", "Run find" };
static const Text kActionFindClear = { "すべて表示", "Show all" };
static const Text kActionCardPdf = { "カードをPDFで保存",
	"Save the card as PDF" };
static const Text kActionCardImage = { "カードを画像で保存",
	"Save the card as an image" };
static const Text kActionSound = { "音を鳴らす", "Play a sound" };
static const Text kActionVideo = { "動画を再生する", "Play a video" };

// Sound and video of a button
static const Text kSectionMedia = { "音・動画", "Sound / video" };
static const Text kMediaFile = { "ファイル", "File" };
static const Text kMediaNone = { "(なし)", "(none)" };
static const Text kMediaMissing = { "音・動画のファイルが設定されていません",
	"No sound or video file is set" };
static const Text kMediaKindMismatch = {
	"音のボタンには音のファイルを、動画のボタンには動画のファイルを選んでください",
	"Choose a sound file for a sound button, a video file for a video button" };
static const Text kMediaUnreadable = { "音・動画のファイルとして読めません",
	"Not a sound or video file" };
static const Text kSoundFailed = { "音を再生できませんでした",
	"Could not play the sound" };
static const Text kVideoFailed = { "動画を開けませんでした",
	"Could not open the video" };

// Switch effects
static const Text kSectionSwitch = { "スイッチの動作(オンのとき)",
	"Switch action (while on)" };
static const Text kEffect = { "動作", "Action" };
static const Text kEffectNone = { "なし", "None" };
static const Text kEffectLockCard = { "このカードの編集をロック",
	"Lock this card" };
static const Text kEffectLock = { "指定した部品をロック", "Lock the chosen parts" };
static const Text kEffectHide = { "指定した部品を隠す", "Hide the chosen parts" };
static const Text kEffectTargets = { "対象の部品", "Parts" };
static const Text kEffectNoTargets = { "(対象にできる部品がありません)",
	"(no parts to choose)" };
static const Text kPartLocked = { "この部品はスイッチでロックされています",
	"This part is locked by a switch" };

// Card image, image viewer
static const Text kSaveCardImageTitle = { "カードを画像で保存",
	"Save the card as an image" };
static const Text kImageSaveFailed = { "画像を保存できませんでした",
	"Could not save the image" };
static const Text kImageSaved = { "画像を保存しました: %s", "Saved the image: %s" };

// Application settings
static const Text kPrefsMenu = { "設定…", "Settings…" };
static const Text kPrefsTitle = { "連札の設定", "Renfuda settings" };
static const Text kPrefsBrowser = { "リンクを開くブラウザ", "Browser for links" };
static const Text kPrefsBrowserDefault = { "システムの既定のブラウザ",
	"The system's default browser" };
static const Text kPrefsBrowserApp = { "次のアプリケーション:",
	"This application:" };
static const Text kPrefsBrowserNote = {
	"http と https のリンクに使います。メールのリンクは、システムの既定のアプリで開きます。",
	"Used for http and https links. Mail links open in the system's default "
	"application." };
static const Text kPrefsChooseApp = { "ブラウザを選ぶ", "Choose a browser" };
static const Text kPrefsBrowserMissing = {
	"設定したブラウザが見つからないので、既定のブラウザで開きました",
	"The chosen browser was not found; opened in the default browser" };
static const Text kActionUnknown = { "(未対応の動作)", "(unknown action)" };

static const Text kUnsetFramesLeft = {
	"型が未設定の枠が %d 個あります。\nプレイモードでは表示されません。",
	"%d frames have no type yet.\nThey are not shown in play mode." };
static const Text kValuesRemain = {
	"この項目の値は %d 枚のカードに残ります。\n"
	"「スタックを整理」で削除できます。",
	"The values of this field remain in %d cards.\n"
	"\"Clean up stack\" removes them." };
static const Text kKindChangeQuestion = {
	"このテンプレートを使うカードは %d 枚。"
	"そのうち %d 枚の値を変換できます。%d 枚は変換できません。\n\n"
	"「変換する」: 変換できた値を書き換えます。元に戻せません。\n"
	"「値は変換せず保持する」: 型が合わない値は残ります。"
	"ただし、新しい型で値を入力すると、古い値は上書きされます。",
	"%d cards use this template. %d values can be converted, %d cannot.\n\n"
	"\"Convert\": converted values are replaced. This cannot be undone.\n"
	"\"Keep the values\": values of the old type are kept, but typing a "
	"new value replaces the old one." };
static const Text kConvertDone = {
	"%d 個の値を変換しました。%d 個は変換できず、そのまま保持しています。",
	"%d values were converted. %d could not be converted and are kept." };
static const Text kImageDropQuestion = {
	"この画像をどう使いますか?", "How should this image be used?" };
static const Text kImageAsBackground = { "背景にする", "As background" };
static const Text kImageAsFrame = { "画像枠として置く", "As image part" };

// Play mode
static const Text kImageMissing = { "画像が見つかりません",
	"Image not found" };
static const Text kImageDrop = { "画像をドロップ", "Drop an image" };
static const Text kImageFromFile = { "ファイルから設定…", "Set from file…" };
static const Text kImageClear = { "クリア", "Clear" };
static const Text kOpenLink = { "リンクを開く", "Open link" };
static const Text kImageUnreadable = { "この画像ファイルは読み込めません。",
	"This image file cannot be read." };
static const Text kNumberInvalid = {
	"数値として読めません。入力前の値に戻しました。",
	"This is not a number. The previous value was restored." };
static const Text kOldValueWarning = {
	"この項目には、型を変える前の値が残っています。\n"
	"入力すると、その値は上書きされます。",
	"This field still holds a value of its previous type.\n"
	"Typing replaces that value." };
static const Text kTemplateMissing = {
	"このカードのテンプレートが見つかりません。値の一覧を表示しています。",
	"The template of this card is missing. Showing its values." };
static const Text kDeleteCardQuestion = {
	"このカードを削除しますか?\nカードのファイルはゴミ箱に移動します。",
	"Delete this card?\nIts file is moved to the Trash." };
static const Text kNotFound = { "条件に合うカードはありません。",
	"No cards match." };
static const Text kFindInvalidNumber = {
	"数字の項目の条件を読めません。\n例: 10、10..20、>=100、<100",
	"A number condition cannot be read.\nExamples: 10, 10..20, >=100, <100" };
static const Text kGoToTitle = { "カード番号へ移動", "Go to card" };
static const Text kGoToLabel = { "カード番号", "Card number" };
static const Text kLinkQuestion = {
	"このスタックのリンクを開きますか?\n%s",
	"Open this link of the stack?\n%s" };
static const Text kLinkTrust = { "このスタックでは、確認しない",
	"Don't ask again for this stack" };
static const Text kLinkBlocked = { "このリンクは開けません(%s)",
	"This link cannot be opened (%s)" };
static const Text kLinkOpenFailed = { "リンクを開けませんでした",
	"The link could not be opened" };

// Saving
static const Text kSaveFailed = {
	"カードを保存できませんでした(%s)。\n入力した内容は保持しています。",
	"The card could not be saved (%s).\nYour input is kept." };
static const Text kTemplateSaveFailed = {
	"テンプレートを保存できませんでした(%s)。",
	"The template could not be saved (%s)." };
static const Text kConflict = {
	"このカードのファイルは、ほかで変更されています。",
	"The file of this card was changed elsewhere." };
static const Text kTemplateConflict = {
	"このテンプレートのファイルは、ほかで変更されています。",
	"The file of this template was changed elsewhere." };
static const Text kUnsavedChanges = { "保存されていない変更があります。",
	"There are unsaved changes." };
static const Text kLogFailed = {
	"ログを書き込めませんでした。操作は続けられます。",
	"The log could not be written. You can keep working." };

// Stack management
static const Text kCleanupQuestion = {
	"孤立した値: %d 枚のカードに %d 個\n"
	"使われていない画像: %d 個\n"
	"一時ファイル: %d 個\n\n"
	"整理すると元に戻せません。",
	"Orphaned values: %d in %d cards\n"
	"Unused images: %d\n"
	"Temporary files: %d\n\n"
	"This cannot be undone." };
static const Text kCleanupMediaSkipped = {
	"\n(読めないファイルがあるため、画像の削除は行いません)",
	"\n(Images are not deleted because some files cannot be read)" };
static const Text kCleanupNothing = { "整理するものはありません。",
	"There is nothing to clean up." };
static const Text kCleanupDone = { "整理しました。", "Cleaned up." };
static const Text kProblemsTitle = { "問題リスト", "Problems" };
static const Text kProblemsNone = { "問題はありません。", "No problems." };
static const Text kSettingsTitle = { "スタックの設定", "Stack settings" };
static const Text kStackTitle = { "スタックの名前", "Stack name" };
static const Text kTemplatesTitle = { "テンプレートの管理",
	"Manage templates" };
static const Text kTemplateNew = { "新規", "New" };
static const Text kTemplateDuplicate = { "複製", "Duplicate" };
static const Text kTemplateName = { "名前", "Name" };
static const Text kTemplateWidth = { "幅", "Width" };
static const Text kTemplateHeight = { "高さ", "Height" };
static const Text kTemplateTitlePart = { "タイトルに使う項目",
	"Title field" };
static const Text kTemplateTitlePartAuto = { "(最初のテキスト)",
	"(first text field)" };
static const Text kTemplateDefault = { "新規カードの既定にする",
	"Default for new cards" };
static const Text kTemplateUsage = { "%s (%d 枚)", "%s (%d cards)" };
static const Text kTemplateDeleteQuestion = {
	"このテンプレートを削除しますか?\n"
	"使っているカードが %d 枚あります。それらのカードの値は残りますが、"
	"「テンプレート欠落」の状態になります。",
	"Delete this template?\n"
	"%d cards use it. Their values are kept, but they will have no "
	"template." };
static const Text kTemplateDeleteUnused = {
	"このテンプレートを削除しますか?", "Delete this template?" };
static const Text kTemplateSizeInvalid = {
	"カードの大きさは 64〜8192 の範囲で指定してください。",
	"The card size must be between 64 and 8192." };
static const Text kTemplateCopySuffix = { "のコピー", " copy" };

// Problem reasons
static const Text kReasonInvalid = { "壊れたファイル", "Broken file" };
static const Text kReasonLimit = { "大きすぎるファイル", "File too large" };
static const Text kReasonEncrypted = { "未対応の暗号化ファイル",
	"Encrypted file (not supported)" };
static const Text kReasonNewer = { "新しい版の形式", "Newer format" };
static const Text kReasonDuplicate = { "IDの重複(採用されなかったファイル)",
	"Duplicate ID (file not used)" };
static const Text kReasonMissingTemplate = { "テンプレート欠落",
	"Template missing" };
static const Text kReasonThemeMissing = { "テーマが見つからない",
	"Theme missing" };
static const Text kReasonOther = { "読み込めないファイル",
	"Unreadable file" };

// Opening
static const Text kOpenFailed = { "スタックを開けませんでした。\n%s",
	"The stack could not be opened.\n%s" };
static const Text kOpenNewer = {
	"このスタックは新しい版のアプリで作られています。",
	"This stack was made by a newer version of the application." };
static const Text kOpenEncrypted = {
	"暗号化されたスタックには、まだ対応していません。",
	"Encrypted stacks are not supported yet." };
static const Text kOpenInvalid = {
	"スタックのマニフェスト(.rfdm)が見つからないか、壊れています。",
	"The manifest (.rfdm) of the stack is missing or broken." };
static const Text kCreateFailed = { "スタックを作成できませんでした(%s)。",
	"The stack could not be created (%s)." };
static const Text kCreateExists = { "同じ名前のフォルダがすでにあります。",
	"A folder with this name already exists." };

// List window
static const Text kListTitle = { "%s — 一覧", "%s — List" };
static const Text kListSearch = { "検索", "Search" };
static const Text kListTemplate = { "テンプレート", "Template" };
static const Text kListImageYes = { "あり", "Yes" };
static const Text kListImageNo = { "なし", "No" };
static const Text kListCount = { "%d 件", "%d cards" };
static const Text kListOpenCard = { "カードを開く", "Open card" };

// PDF (I/O spec 4)
static const Text kPdfTitle = { "PDFで保存", "Save as PDF" };
static const Text kPdfPrintBackground = { "背景を印刷する",
	"Print backgrounds" };
static const Text kPdfImageColumn = { "画像の列", "Image columns" };
static const Text kPdfImageText = { "「あり」「なし」", "\"Yes\" / \"No\"" };
static const Text kPdfImageThumb = { "サムネイル", "Thumbnails" };
static const Text kPdfProgress = { "PDFを書き出しています… %d / %d",
	"Writing the PDF… %d / %d" };
static const Text kPdfDone = { "PDFを保存しました。", "The PDF was saved." };
static const Text kPdfFailed = { "PDFを保存できませんでした(%s)。",
	"The PDF could not be saved (%s)." };
static const Text kPdfExists = { "同じ名前のファイルがあります。上書きしますか?",
	"A file with this name exists. Replace it?" };
static const Text kPdfListSuffix = { "一覧", "list" };
static const Text kPdfCardsFormat = { "%s-%d枚.pdf", "%s-%d cards.pdf" };
static const Text kPdfFooter = { "%s ・ %s ・ %d / %d", "%s · %s · %d / %d" };

// CSV wizard (I/O spec 3.3)
static const Text kCsvTitle = { "CSVの取り込み", "Import CSV" };
static const Text kCsvStepFile = { "1. ファイルを選ぶ", "1. Choose a file" };
static const Text kCsvStepFormat = { "2. 形式の確認", "2. Format" };
static const Text kCsvStepColumns = { "3. 列の割り当て", "3. Columns" };
static const Text kCsvStepTemplate = { "4. テンプレート", "4. Template" };
static const Text kCsvStepImages = { "5. 画像", "5. Images" };
static const Text kCsvStepConfirm = { "6. 確認", "6. Confirm" };
static const Text kCsvStepProgress = { "7. 取り込み中", "7. Importing" };
static const Text kCsvStepDone = { "8. 完了", "8. Done" };
static const Text kCsvChooseFile = { "CSVファイルを選ぶ…",
	"Choose a CSV file…" };
static const Text kCsvFileInfo = { "%s(%s)", "%s (%s)" };
static const Text kCsvNoFile = { "ファイルが選ばれていません。",
	"No file chosen." };
static const Text kCsvEncoding = { "文字コード", "Encoding" };
static const Text kCsvAuto = { "自動(%s)", "Automatic (%s)" };
static const Text kCsvAutoPlain = { "自動", "Automatic" };
static const Text kCsvDelimiter = { "区切り", "Delimiter" };
static const Text kCsvComma = { "カンマ", "Comma" };
static const Text kCsvTab = { "タブ", "Tab" };
static const Text kCsvSemicolon = { "セミコロン", "Semicolon" };
static const Text kCsvHeader = { "先頭の行を見出しとして使う",
	"The first row holds the names" };
static const Text kCsvSkip = { "先頭から読み飛ばす行", "Rows to skip" };
static const Text kCsvColumn = { "列", "Column" };
static const Text kCsvUsage = { "使い方", "Use as" };
static const Text kCsvSample = { "先頭の値", "First value" };
static const Text kCsvIgnore = { "無視する", "Ignore" };
static const Text kCsvText = { "テキスト", "Text" };
static const Text kCsvNumber = { "数字", "Number" };
static const Text kCsvBool = { "スイッチ", "Switch" };
static const Text kCsvImageUrl = { "画像(URL)", "Image (URL)" };
static const Text kCsvImagePath = { "画像(ファイルのパス)",
	"Image (file path)" };
static const Text kCsvLinkUrl = { "リンク(URL)", "Link (URL)" };
static const Text kCsvMultiline = { "複数行", "Multi-line" };
static const Text kCsvKeepUrl = { "URLを項目として残す",
	"Keep the URL as a field" };
static const Text kCsvMakeButton = { "リンクを開くボタンを作る",
	"Make a button that opens it" };
static const Text kCsvLinkImage = { "画像のリンク先にする",
	"Image links here" };
static const Text kCsvTitleColumn = { "タイトル", "Title" };
static const Text kCsvTemplateAuto = { "自動で作る",
	"Make one automatically" };
static const Text kCsvTemplateExisting = { "既存のテンプレートを使う",
	"Use an existing template" };
static const Text kCsvTargetPart = { "取り込む項目", "Into field" };
static const Text kCsvFetchWhen = { "取得のタイミング", "When to fetch" };
static const Text kCsvFetchNow = { "取り込みと同時に取得する",
	"While importing" };
static const Text kCsvFetchBackground = {
	"カードを作成したあと、バックグラウンドで取得する",
	"In the background after the cards are made" };
static const Text kCsvFetchNotice = {
	"画像の取得元の利用規約と著作権は、ご自身で確認してください。"
	"取得を禁止するサイトからは取得しないでください。",
	"Please check the terms of use and copyright of the image sources. "
	"Do not fetch from sites that forbid it." };
static const Text kCsvNoImages = { "画像の列はありません。",
	"There are no image columns." };
static const Text kCsvThumbSize = { "サムネイルの大きさ", "Thumbnail size" };
static const Text kCsvConfirmText = {
	"取り込む行: %d\n列数が合わない行: %d\n作られる項目: %s",
	"Rows to import: %d\nRows with a different number of fields: %d\n"
	"Fields: %s" };
static const Text kCsvScanning = { "ファイルを調べています…",
	"Reading the file…" };
static const Text kCsvTooLarge = {
	"このファイルは取り込めません(%s)。\n"
	"上限: ファイル 512 MiB、1,000,000 行、1つの値 1 MiB。",
	"This file cannot be imported (%s).\n"
	"Limits: 512 MiB, 1,000,000 rows, 1 MiB per value." };
static const Text kCsvDestination = { "保存先", "Save to" };
static const Text kCsvStackName = { "スタックの名前", "Stack name" };
static const Text kCsvImport = { "取り込む", "Import" };
static const Text kCsvProgressText = {
	"作成したカード: %d\n取得した画像: %d / %d",
	"Cards made: %d\nImages fetched: %d / %d" };
static const Text kCsvOpenStack = { "スタックを開く", "Open stack" };
static const Text kCsvSaveReport = { "レポートを保存…", "Save report…" };
static const Text kCsvAppendCanceled = {
	"取り込みを中止しました。作成済みの %d 枚のカードは残っています。",
	"The import was canceled. The %d cards made so far are kept." };
static const Text kCsvFailed = { "取り込めませんでした(%s)。",
	"The import failed (%s)." };
static const Text kCsvReportName = { "取り込みレポート.txt",
	"Import report.txt" };

// Icon chooser (theme spec 8.4)
static const Text kIconTitle = { "アイコンを選ぶ", "Choose an icon" };
static const Text kIconSearch = { "検索", "Search" };
static const Text kIconFromFile = { "ファイルから…", "From file…" };
static const Text kIconNone = { "アイコンが見つかりません。",
	"No icons were found." };

// MIME descriptions
static const Text kMimeManifest = { "連札 スタック", "Renfuda stack" };
static const Text kMimeTemplate = { "連札 テンプレート", "Renfuda template" };
static const Text kMimeCard = { "連札 カード", "Renfuda card" };
static const Text kMimeTheme = { "連札 テーマ", "Renfuda theme" };


}	// namespace str

#endif	// BDB_STRINGS_H
