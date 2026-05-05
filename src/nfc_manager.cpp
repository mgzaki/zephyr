/**
 * @file nfc_manager.cpp
 * @brief NFC T2T NDEF tag — BLE OOB + AAR records for secure tap-to-pair.
 *
 * NDEF message layout:
 *   Record 0  — BLE OOB  (MIME type: application/vnd.bluetooth.le.oob)
 *     Payload:  [BD_ADDR 6 bytes LE] [AD structures ...]
 *               AD type 0x08  Short local name  "HaykulTracker"
 *               AD type 0x07  Complete 128-bit service UUID list
 *                             → 0000feaa-0000-1000-8000-00805f9b34fb
 *   Record 1  — Android Application Record
 *     Payload:  "com.example.haykulfinder"
 *
 * The Android app's NfcPairingManager.parseBleOobRecord() inspects the AD
 * structures and rejects any tag that does NOT advertise the Haykul service
 * UUID, preventing pairing with third-party BLE devices via NFC.
 */

#include "nfc_manager.h"
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(nfc_manager, LOG_LEVEL_INF);

#ifdef CONFIG_NFC_T2T_NRFXLIB

#include <nfc_t2t_lib.h>
#include <nfc/ndef/msg.h>
#include <nfc/ndef/le_oob_rec.h>
#include <nfc/ndef/aar_rec.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <string.h>

/* ── Constants ───────────────────────────────────────────────────────────── */

/** Android app package name — used in the AAR record. */
static const char APP_PACKAGE[] = "com.example.haykulfinder";

/**
 * Haykul service UUID in little-endian byte order (required by BLE AD format).
 * UUID: 0000feaa-0000-1000-8000-00805f9b34fb
 */
static const uint8_t HAYKUL_UUID_LE[16] = {
    0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00,  /* bytes 0-5 */
    0x00, 0x80,                           /* bytes 6-7 */
    0x00, 0x10,                           /* bytes 8-9 */
    0x00, 0x00,                           /* bytes 10-11 */
    0xaa, 0xfe, 0x00, 0x00                /* bytes 12-15 */
};

/** Device name embedded in OOB AD structure (short local name, type 0x08). */
static const char DEVICE_NAME[] = "HaykulTracker";

/* ── NDEF Buffer ─────────────────────────────────────────────────────────── */

/** Scratch buffer for the NDEF message.  Sized conservatively. */
#define NDEF_BUF_SIZE 256U
static uint8_t ndef_buf[NDEF_BUF_SIZE];

/* ── OOB Payload Builder ─────────────────────────────────────────────────── */

/**
 * @brief Build the raw OOB payload bytes.
 *
 * OOB payload format:
 *   Offset  0-5   BD_ADDR (little-endian)
 *   Offset  6+    AD structures, each: [len][type][data...]
 *     AD for short local name: [len=1+namelen][0x08][name...]
 *     AD for 128-bit UUIDs:    [len=1+16][0x07][UUID 16 bytes LE]
 *
 * @param[out] out   Output buffer.
 * @param[in]  size  Size of output buffer.
 * @return Number of bytes written, or negative on error.
 */
static int build_oob_payload(uint8_t *out, size_t size)
{
    /* Retrieve public BD_ADDR from the Bluetooth stack. */
    bt_addr_le_t addr;
    size_t       count = 1U;
    bt_id_get(&addr, &count);
    if (count == 0U) {
        LOG_ERR("bt_id_get returned no address");
        return -ENODEV;
    }

    uint8_t name_len  = (uint8_t)strlen(DEVICE_NAME);
    /* Total size: 6 (addr) + 2 (len+type) + name_len + 2 (len+type) + 16 (UUID) */
    size_t total = 6U + 2U + name_len + 2U + 16U;
    if (total > size) return -ENOMEM;

    uint8_t *p = out;

    /* BD_ADDR — little-endian (nRF stores it LE naturally). */
    memcpy(p, addr.a.val, 6U);
    p += 6U;

    /* AD: Short Local Name (type 0x08). */
    *p++ = (uint8_t)(1U + name_len); /* length field */
    *p++ = 0x08U;                    /* AD type: Shortened Local Name */
    memcpy(p, DEVICE_NAME, name_len);
    p += name_len;

    /* AD: Complete 128-bit UUID list (type 0x07). */
    *p++ = 1U + 16U; /* length field */
    *p++ = 0x07U;    /* AD type: Complete list of 128-bit UUIDs */
    memcpy(p, HAYKUL_UUID_LE, 16U);
    p += 16U;

    return (int)(p - out);
}

/* ── NFC Callback ────────────────────────────────────────────────────────── */

/**
 * @brief NFC T2T event callback.
 * @param context  User-defined context (unused).
 * @param event    NFC library event type.
 * @param data     Event-specific data pointer (unused here).
 * @param data_len Length of event data (unused here).
 */
static void nfc_callback(void *context, nfc_t2t_event_t event,
                          const uint8_t *data, size_t data_len)
{
    ARG_UNUSED(context);
    ARG_UNUSED(data);
    ARG_UNUSED(data_len);

    switch (event) {
    case NFC_T2T_EVENT_FIELD_ON:
        LOG_INF("NFC field detected — phone tapping device");
        break;
    case NFC_T2T_EVENT_FIELD_OFF:
        LOG_INF("NFC field removed");
        break;
    default:
        break;
    }
}

#endif /* CONFIG_NFC_T2T_NRFXLIB */

/* ── NfcManager Implementation ───────────────────────────────────────────── */

NfcManager::NfcManager() {}
NfcManager::~NfcManager() { stop(); }

int NfcManager::init()
{
#ifndef CONFIG_NFC_T2T_NRFXLIB
    LOG_WRN("NFC support not compiled in (CONFIG_NFC_T2T_NRFXLIB not set)");
    return 0;
#else
    /* Build raw OOB payload. */
    uint8_t oob_payload[64];
    int oob_len = build_oob_payload(oob_payload, sizeof(oob_payload));
    if (oob_len < 0) {
        LOG_ERR("Failed to build OOB payload (%d)", oob_len);
        return oob_len;
    }

    /* Construct NDEF records. */
    NFC_NDEF_MSG_DEF(ndef_msg, 2U);

    /* Record 0: BLE OOB MIME record. */
    NFC_NDEF_LE_OOB_RECORD_DESC_DEF(oob_rec, oob_payload, (uint32_t)oob_len);

    /* Record 1: Android Application Record. */
    NFC_NDEF_AAR_RECORD_DESC_DEF(aar_rec, APP_PACKAGE);

    int err = nfc_ndef_msg_record_add(&NFC_NDEF_MSG(ndef_msg),
                                      &NFC_NDEF_LE_OOB_RECORD_DESC(oob_rec));
    if (err) {
        LOG_ERR("OOB record add failed (%d)", err);
        return err;
    }

    err = nfc_ndef_msg_record_add(&NFC_NDEF_MSG(ndef_msg),
                                  &NFC_NDEF_AAR_RECORD_DESC(aar_rec));
    if (err) {
        LOG_ERR("AAR record add failed (%d)", err);
        return err;
    }

    /* Encode the NDEF message into the flat buffer. */
    uint32_t buf_len = (uint32_t)sizeof(ndef_buf);
    err = nfc_ndef_msg_encode(&NFC_NDEF_MSG(ndef_msg), ndef_buf, &buf_len);
    if (err) {
        LOG_ERR("NDEF encode failed (%d)", err);
        return err;
    }

    /* Initialise the T2T library and register the tag payload. */
    err = nfc_t2t_setup(nfc_callback, NULL);
    if (err) {
        LOG_ERR("nfc_t2t_setup failed (%d)", err);
        return err;
    }

    err = nfc_t2t_payload_set(ndef_buf, buf_len);
    if (err) {
        LOG_ERR("nfc_t2t_payload_set failed (%d)", err);
        return err;
    }

    err = nfc_t2t_emulation_start();
    if (err) {
        LOG_ERR("nfc_t2t_emulation_start failed (%d)", err);
        return err;
    }

    LOG_INF("NFC T2T tag active — OOB payload %d bytes, UUID validated by app",
            oob_len);
    return 0;
#endif /* CONFIG_NFC_T2T_NRFXLIB */
}

void NfcManager::stop()
{
#ifdef CONFIG_NFC_T2T_NRFXLIB
    nfc_t2t_emulation_stop();
    LOG_INF("NFC T2T stopped");
#endif
}
