#pragma once

#include "esphome/core/defines.h"

#include "kyo_protocol.h"

namespace esphome::kyo_alarm {

// Event descriptions, in the order of KyoEventType. Only the language chosen in the configuration is compiled in.
// Texts of the BIS-KYO documentation, except remote command (not documented) and unknown events.
#ifdef USE_KYO_ALARM_EVENT_LANGUAGE_IT
static const char *const KYO_EVENT_TEXTS[] = {
    "Allarme Area",
    "Allarme Zona",
    "Inattività Area",
    "Negligenza Area",
    "Esclusione Zona",
    "Reinclusione Zona",
    "Riconosciuto Codice",
    "Riconosciuta Chiave",
    "Auto Esclusione Zona",
    "Inserimento Area",
    "Disinserimento Area",
    "Richiesta Inserimento Area",
    "Richiesta Disinserimento Area",
    "Reset Memoria Area",
    "Disinserimento sotto Costrizione Area",
    "Chiamata Fallita",
    "Sabotaggio Zona",
    "Ripristino Zona",
    "Comando Remoto",
    "Evento",
};
#else
static const char *const KYO_EVENT_TEXTS[] = {
    "Alarm Partition",
    "Alarm Zone",
    "Inactivity Partition",
    "Negligence Partition",
    "Bypass Zone",
    "Unbypass Zone",
    "Valid Code",
    "Valid Key",
    "Auto-bypass Zone",
    "Arm Partition",
    "Disarm Partition",
    "Request Arm Partition",
    "Request Disarm Partition",
    "Reset Memory Partition",
    "Disarm under Duress Partition",
    "Failed Call",
    "Tamper Zone",
    "Restore Zone",
    "Remote Command",
    "Event",
};
#endif

static_assert(sizeof(KYO_EVENT_TEXTS) / sizeof(KYO_EVENT_TEXTS[0]) == size_t(KyoEventType::UNKNOWN) + 1,
              "One text per event type");

}  // namespace esphome::kyo_alarm
