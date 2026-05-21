pragma Singleton
import QtQuick

QtObject {
    id: theme

    property string themeId: "light"
    function setTheme(id) { themeId = id }
    function nextTheme() {
        const ids = themes.map(t => t.id)
        const i = ids.indexOf(themeId)
        themeId = ids[(i + 1) % ids.length]
    }

    readonly property var themes: [
        { id: "light",    name: "Light",    dark: false },
        { id: "ivory",    name: "Ivory",    dark: false },
        { id: "dark",     name: "Dark",     dark: true  },
        { id: "midnight", name: "Midnight", dark: true  },
        { id: "velvet",   name: "Velvet",   dark: true  }
    ]

    readonly property bool isDark: _p.dark

    readonly property QtObject _p:
        themeId === "light"    ? _palLight :
        themeId === "midnight" ? _palMidnight :
        themeId === "velvet"   ? _palVelvet :
        themeId === "ivory"    ? _palIvory :
                                 _palDark

    readonly property color bg:           _p.bg
    readonly property color bgElevated:   _p.bgElevated
    readonly property color surface:      _p.surface
    readonly property color surfaceHi:    _p.surfaceHi
    readonly property color border:       _p.border
    readonly property color borderStrong: _p.borderStrong
    readonly property color overlay:      _p.overlay

    readonly property color textPrimary:   _p.textPrimary
    readonly property color textSecondary: _p.textSecondary
    readonly property color textTertiary:  _p.textTertiary
    readonly property color textDisabled:  _p.textDisabled

    readonly property color accent:      _p.accent
    readonly property color accentHi:    _p.accentHi
    readonly property color accentLo:    _p.accentLo
    readonly property color accentSoft:  _p.accentSoft
    readonly property color accentInk:   _p.accentInk

    readonly property color gradTint:    _p.gradTint

    readonly property color sky:         "#66CCFF"
    readonly property color skySoft:     isDark ? "#66CCFF22" : "#66CCFF1F"
    readonly property color midnight:    "#003366"
    readonly property color violet:      "#6600FF"
    readonly property color violetSoft:  isDark ? "#6600FF22" : "#6600FF14"

    // "Call" cluster — used by the dialer's place-call button. Green to
    // signal "start the call"; the active-call view uses Theme.danger for
    // the hangup button so the two intents are visually distinct.
    readonly property color call:        "#1FA75A"
    readonly property color callHi:      "#26C26A"
    readonly property color callLo:      "#178548"
    readonly property color callGlow:    isDark ? "#1FA75A55" : "#1FA75A33"

    readonly property color success:     "#22C55E"
    readonly property color successSoft: isDark ? "#22C55E22" : "#22C55E1A"
    readonly property color warning:     "#F59E0B"
    readonly property color warningSoft: isDark ? "#F59E0B22" : "#F59E0B1A"
    readonly property color danger:      "#FF5349"
    readonly property color dangerHi:    "#FF7268"
    readonly property color dangerSoft:  isDark ? "#FF534922" : "#FF53491A"
    readonly property color info:        "#0EA5E9"

    readonly property color accentInk2:  "#FFFFFF"

    property QtObject _palDark: QtObject {
        property bool dark: true
        property color bg:           "#0A0F1A"
        property color bgElevated:   "#101724"
        property color surface:      "#141C2B"
        property color surfaceHi:    "#1B2436"
        property color border:       "#222C40"
        property color borderStrong: "#2F3A52"
        property color overlay:      "#000000A0"
        property color textPrimary:   "#EDF1F8"
        property color textSecondary: "#9AA5BE"
        property color textTertiary:  "#6A7796"
        property color textDisabled:  "#444E66"
        property color accent:     "#FF5349"
        property color accentHi:   "#FF7268"
        property color accentLo:   "#E63D33"
        property color accentSoft: "#FF534922"
        property color accentInk:  "#1A0606"
        property color gradTint:   Qt.rgba(1.0, 0.325, 0.286, 0.08)
    }

    property QtObject _palLight: QtObject {
        property bool dark: false
        property color bg:           "#FFFFFF"
        property color bgElevated:   "#FFFFFF"
        property color surface:      "#FFFFFF"
        property color surfaceHi:    "#F2F4F8"
        property color border:       "#E1E5EC"
        property color borderStrong: "#C8CED7"
        property color overlay:      "#0F172A66"
        property color textPrimary:   "#0F172A"
        property color textSecondary: "#475569"
        property color textTertiary:  "#64748B"
        property color textDisabled:  "#94A3B8"
        property color accent:     "#003366"
        property color accentHi:   "#1A4D80"
        property color accentLo:   "#002247"
        property color accentSoft: "#E6ECF3"
        property color accentInk:  "#FFFFFF"
        property color gradTint:   "transparent"
    }

    // Midnight = the dock icon, large. Dark navy ground (the phone glyph's
    // ink color) with the red-orange "squawk" dot as the accent.
    property QtObject _palMidnight: QtObject {
        property bool dark: true
        property color bg:           "#061325"
        property color bgElevated:   "#0A1E3C"
        property color surface:      "#0E2240"
        property color surfaceHi:    "#14305A"
        property color border:       "#1F3F6C"
        property color borderStrong: "#2C5288"
        property color overlay:      "#000000C0"
        property color textPrimary:   "#DCE7F4"
        property color textSecondary: "#8FA8C7"
        property color textTertiary:  "#5E789A"
        property color textDisabled:  "#3A4F6C"
        property color accent:     "#FF5349"
        property color accentHi:   "#FF7268"
        property color accentLo:   "#E63D33"
        property color accentSoft: "#FF534926"
        property color accentInk:  "#1A0606"
        property color gradTint:   Qt.rgba(1.0, 0.325, 0.286, 0.10)
    }

    property QtObject _palVelvet: QtObject {
        property bool dark: true
        property color bg:           "#0F0A1F"
        property color bgElevated:   "#150F2A"
        property color surface:      "#1A1230"
        property color surfaceHi:    "#251945"
        property color border:       "#2F2452"
        property color borderStrong: "#42316E"
        property color overlay:      "#000000B0"
        property color textPrimary:   "#ECE6F8"
        property color textSecondary: "#A89AC7"
        property color textTertiary:  "#6E6296"
        property color textDisabled:  "#48405A"
        property color accent:     "#8B5CF6"
        property color accentHi:   "#A78BF9"
        property color accentLo:   "#6D3FE0"
        property color accentSoft: "#8B5CF624"
        property color accentInk:  "#15082E"
        property color gradTint:   Qt.rgba(0.55, 0.36, 0.96, 0.10)
    }

    property QtObject _palIvory: QtObject {
        property bool dark: false
        property color bg:           "#FBF6F2"
        property color bgElevated:   "#FFFFFF"
        property color surface:      "#FFFFFF"
        property color surfaceHi:    "#F2EAE3"
        property color border:       "#E8DCD2"
        property color borderStrong: "#D0BFB1"
        property color overlay:      "#1F161155"
        property color textPrimary:   "#1F1611"
        property color textSecondary: "#5A4F47"
        property color textTertiary:  "#837569"
        property color textDisabled:  "#B5A795"
        property color accent:     "#FF5349"
        property color accentHi:   "#FF7268"
        property color accentLo:   "#E63D33"
        property color accentSoft: "#FAE0DA"
        property color accentInk:  "#1F0808"
        property color gradTint:   Qt.rgba(1.0, 0.45, 0.4, 0.08)
    }

    readonly property int s2:  2
    readonly property int s4:  4
    readonly property int s6:  6
    readonly property int s8:  8
    readonly property int s10: 10
    readonly property int s12: 12
    readonly property int s14: 14
    readonly property int s16: 16
    readonly property int s20: 20
    readonly property int s24: 24
    readonly property int s32: 32
    readonly property int s40: 40

    readonly property int rowSm: 44
    readonly property int rowMd: 52
    readonly property int rowLg: 60

    readonly property int r4:  4
    readonly property int r6:  6
    readonly property int r8:  8
    readonly property int r10: 10
    readonly property int r12: 12
    readonly property int r14: 14
    readonly property int r16: 16
    readonly property int rFull: 999

    readonly property int fxs:   9
    readonly property int fsm:   10
    readonly property int fbody: 11
    readonly property int flg:   12
    readonly property int fxl:   14
    readonly property int f2xl:  17
    readonly property int f3xl:  22
    readonly property int f4xl:  30

    readonly property string fontFamily: Qt.platform.os === "osx"
        ? ".AppleSystemUIFont"
        : "Inter, Segoe UI, sans-serif"

    readonly property int dur:  80
    readonly property int durSlow: 180

    function paletteFor(id) {
        return id === "light"    ? _palLight
             : id === "midnight"      ? _palMidnight
             : id === "velvet"        ? _palVelvet
             : id === "ivory"         ? _palIvory
             :                          _palDark
    }
}
