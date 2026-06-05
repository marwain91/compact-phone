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

    // The contact's name when we know it, otherwise the dialled number.
    function peerLabel(displayName, uri) {
        if (displayName && displayName.length > 0)
            return displayName
        return phoneNumber(uri)
    }
}
