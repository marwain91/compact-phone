pragma Singleton
import QtQuick

// Stateless display-formatting helpers shared across views (History, the
// active-call view, …) so SIP URIs are presented the same way everywhere.
QtObject {
    // The bare user part of a SIP URI — the dialled number:
    //   sip:721352367@daktela.daktela.com  ->  721352367
    // Handles a name-addr form (<sip:…>) and strips the scheme and any
    // user-part parameters. Falls back to the raw input if nothing's left.
    function phoneNumber(uri) {
        let s = (uri || "").trim()
        const lt = s.indexOf('<'), gt = s.indexOf('>')
        if (lt >= 0 && gt > lt) s = s.substring(lt + 1, gt) // name-addr form
        s = s.replace(/^sips?:/i, "")                        // strip scheme
        const at = s.indexOf('@')
        if (at >= 0) s = s.substring(0, at)                  // user part only
        const semi = s.indexOf(';')
        if (semi >= 0) s = s.substring(0, semi)              // drop user params
        return s.length > 0 ? s : (uri || "")
    }

    // True when a "display name" is really a SIP URI in disguise — e.g. the
    // remote Contact header (<sip:95.80.200.178:5060;transport=tcp>) that
    // PJSIP hands us when the peer sends no human name. Such strings must not
    // be shown as-is; they should be reduced to the dialled number instead.
    function looksLikeUri(s) {
        return /(^\s*<)|(sips?:)|@/i.test(s || "")
    }

    // The contact's name when we have a real one, otherwise the dialled
    // number. A URI masquerading as a display name is treated as no name.
    function peerLabel(displayName, uri) {
        if (displayName && displayName.length > 0 && !looksLikeUri(displayName))
            return displayName
        return phoneNumber(uri)
    }
}
