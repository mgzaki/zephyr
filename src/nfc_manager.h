/**
 * @file nfc_manager.h
 * @brief NFC Tag Type 2 (T2T) manager — tap-to-pair with unique device identity.
 *
 * The nRF52840 includes a hardware NFC front-end on pins P0.09 (NFCT_S1) and
 * P0.10 (NFCT_C1).  When a phone taps the device, the tag presents an NDEF
 * message containing two records:
 *
 *   1. BLE OOB record  (MIME: "application/vnd.bluetooth.le.oob")
 *      ─ BD_ADDR (6 bytes, little-endian)
 *      ─ AD structures:
 *          • Complete Local Name  (type 0x09)
 *          • Complete 128-bit Service UUIDs (type 0x07) — the Haykul UUID
 *            0000feaa-0000-1000-8000-00805f9b34fb
 *
 *   2. Android Application Record (AAR)
 *      ─ Package: "com.example.haykulfinder"
 *      ─ Ensures the HaykulFinder app is launched (not a generic BLE pairing
 *        dialog) and provides a second layer of authenticity.
 *
 * Uniqueness guarantee
 * ─────────────────────
 * The Android NfcPairingManager validates that the service UUID in the OOB
 * record's AD data matches the Haykul UUID before accepting the pairing.
 * Counterfeit or foreign NFC tags that lack this UUID are silently rejected.
 *
 * Configuration
 * ─────────────
 * Enable in prj.conf:
 *   CONFIG_NFC_T2T_NRFXLIB=y
 *   CONFIG_NFC_NDEF=y
 *   CONFIG_NFC_NDEF_MSG=y
 *   CONFIG_NFC_NDEF_RECORD=y
 *   CONFIG_NFC_NDEF_LE_OOB_REC=y
 *   CONFIG_NFC_NDEF_AAR_REC=y
 */

#ifndef NFC_MANAGER_H
#define NFC_MANAGER_H

class NfcManager {
public:
    NfcManager();
    ~NfcManager();

    /**
     * @brief Start the NFC T2T tag.
     * @return 0 on success, negative error code on failure,
     *         or 0 with a warning log if NFC is not compiled in.
     */
    int init();

    /**
     * @brief Stop the NFC tag hardware (call before deep sleep).
     */
    void stop();
};

#endif // NFC_MANAGER_H
