// SH1106 wiring/init and font follow copych/RDX-Reface-DX-emu (see GUI.md).
// This module runs in ControlTask on Core1 only. No new task or SD access.
#if SAMPLER_GUI
#include <Wire.h>
#include "gui/gui_core.h"

#if defined(CONFIG_IDF_TARGET_ESP32P4)
#error "Configure a conflict-free P4 GUI pinout before enabling SAMPLER_GUI"
#endif

namespace {
sampler_gui::Screen guiScreen;
sampler_gui::Encoder guiEncoder;
sampler_gui::Button guiButtons[5];
const uint8_t guiButtonPins[] = {GUI_ENC_BUTTON, GUI_PREV_BUTTON, GUI_NEXT_BUTTON,
                                GUI_BACK_BUTTON, GUI_PANIC_BUTTON};
bool guiOnline = false, guiDirty = true;
bool guiVisible = false, guiFrameReady = false;
int guiSelected = 0, guiObservedFolder = -1;
enum GuiPage : uint8_t { GUI_HOME, GUI_MENU, GUI_SETS, GUI_VOLUME, GUI_FX_MENU, GUI_FX_SLOT, GUI_FX_EDIT };
GuiPage guiPage = GUI_HOME;
uint8_t guiFxSlot = 0, guiFxField = 0;
uint32_t guiLastPoll = 0, guiLastTransfer = 0, guiLastDraw = 0;
uint32_t guiMaxTransferUs = 0;
uint32_t guiLastRecovery = 0, guiErrors = 0, guiRecoveries = 0;
uint32_t guiEncoderSteps = 0, guiButtonEvents = 0;
uint8_t guiLastWireError = 0;

uint8_t guiEncoderPins() {
    return (digitalRead(GUI_ENC_A) == LOW) | ((digitalRead(GUI_ENC_B) == LOW) << 1);
}

bool guiSend(uint8_t kind, const uint8_t* bytes, size_t count) {
    Wire.beginTransmission(GUI_OLED_ADDRESS);
    Wire.write(kind);
    Wire.write(bytes, count);
    guiLastWireError = Wire.endTransmission();
    if (guiLastWireError) ++guiErrors;
    return guiLastWireError == 0;
}

bool guiInitDisplay() {
    if (!Wire.begin(GUI_SDA, GUI_SCL, 400000)) return false;
    // This is a driver/scheduler timeout, not the expected wire duration.
    // 1 ms proved too short on the actual S3 and left a partially drawn screen.
    Wire.setTimeOut(10);
    const uint8_t init[] = {0xae, 0xd5, 0x80, 0xa8, 63, 0xd3, 0, 0x40,
        0xad, 0x8b, 0xa1, 0xc8, 0xda, 0x12, 0x81, 0x7f, 0xd9, 0x22,
        0xdb, 0x40, 0xa4, 0xa6};
    guiVisible = guiFrameReady = false;
    guiDirty = true;
    guiLastDraw = micros() - 100000;
    // Keep panel off until every byte of the first framebuffer has arrived.
    return guiSend(0, init, sizeof(init));
}

void guiSelectSet(int direction) {
    const int count = Sampler.getFolderCount();
    if (count <= 0) return;
    guiSelected = (guiSelected + direction + count) % count;
    guiDirty = true;
}

void guiPanic() {
    Sampler.setSustain(false);
    Sampler.allNotesOff(true);
    guiPage = GUI_HOME;
    guiDirty = true;
}

const char* guiFxName(RdxFxId id) {
    static const char* const names[] = {"THRU", "DISTORT", "TOUCH WAH", "CHORUS", "FLANGER", "PHASER", "DELAY", "REVERB"};
    return id < RDX_FX_COUNT ? names[id] : "THRU";
}
const char* guiFxParamName(RdxFxId id, uint8_t param) {
    if (param == 0) {
        if (id == RDX_FX_DISTORTION) return "DRIVE";
        if (id == RDX_FX_TOUCHWAH) return "SENSE";
        if (id == RDX_FX_DELAY) return "FEEDBACK";
        if (id == RDX_FX_REVERB) return "DEPTH";
        return "DEPTH";
    }
    if (id == RDX_FX_DISTORTION) return "TONE";
    if (id == RDX_FX_TOUCHWAH) return "RESO";
    if (id == RDX_FX_DELAY || id == RDX_FX_REVERB) return "TIME";
    return "RATE";
}

void guiBack() {
    switch (guiPage) {
        case GUI_MENU: guiPage = GUI_HOME; break;
        case GUI_SETS: guiPage = GUI_MENU; guiSelected = 0; break;
        case GUI_VOLUME: guiPage = GUI_MENU; guiSelected = 1; break;
        case GUI_FX_MENU: guiPage = GUI_MENU; guiSelected = 2; break;
        case GUI_FX_SLOT: guiPage = GUI_FX_MENU; guiSelected = guiFxSlot; break;
        case GUI_FX_EDIT: guiPage = GUI_FX_SLOT; break;
        default: break;
    }
    guiDirty = true;
}

void guiEnter() {
    switch (guiPage) {
        case GUI_HOME: guiPage = GUI_MENU; guiSelected = 0; break;
        case GUI_MENU:
            if (guiSelected == 0) { guiPage = GUI_SETS; guiSelected = Sampler.getFolderId(); }
            else if (guiSelected == 1) guiPage = GUI_VOLUME;
            else if (guiSelected == 2) { guiPage = GUI_FX_MENU; guiSelected = 0; }
            else guiPanic();
            break;
        case GUI_SETS:
            if (guiSelected != Sampler.getFolderId()) Sampler.setCurrentFolder(guiSelected);
            guiObservedFolder = Sampler.getFolderId(); guiPage = GUI_HOME;
            break;
        case GUI_VOLUME: guiBack(); break;
        case GUI_FX_MENU: guiFxSlot = guiSelected; guiPage = GUI_FX_SLOT; guiSelected = 0; break;
        case GUI_FX_SLOT: guiFxField = guiSelected; guiPage = GUI_FX_EDIT; break;
        case GUI_FX_EDIT: guiBack(); break;
    }
    guiDirty = true;
}

void guiRotate(int direction) {
    switch (guiPage) {
        case GUI_HOME: break;
        case GUI_MENU: guiSelected = (guiSelected + direction + 4) % 4; break;
        case GUI_SETS: guiSelectSet(direction); return;
        case GUI_VOLUME: Sampler.setMasterVolume(Sampler.getMasterVolume() + direction * .02f); break;
        case GUI_FX_MENU: guiSelected = (guiSelected + direction + 2) % 2; break;
        case GUI_FX_SLOT: guiSelected = (guiSelected + direction + 3) % 3; break;
        case GUI_FX_EDIT:
            if (guiFxField == 0) {
                int v = int(Effects.type(guiFxSlot)) + direction;
                if (v < 0) v = RDX_FX_REVERB; if (v > RDX_FX_REVERB) v = 0;
                Effects.setSlot(guiFxSlot, (RdxFxId)v, Effects.param(guiFxSlot, 0), Effects.param(guiFxSlot, 1));
            } else {
                int v = int(Effects.param(guiFxSlot, guiFxField - 1)) + direction * 2;
                Effects.setParam(guiFxSlot, guiFxField - 1, uint8_t(v < 0 ? 0 : (v > 127 ? 127 : v)));
            }
            break;
    }
    guiDirty = true;
}

void guiClick(unsigned button, sampler_gui::Button::Event event) {
    if (event == sampler_gui::Button::NONE) return;
    ++guiButtonEvents;
    if (button == 4) {
        guiPanic(); return;
    }
    if (button == 0 && event == sampler_gui::Button::HOLD) { guiBack(); return; }
    if (event != sampler_gui::Button::CLICK) return;
    if (button == 1) { guiRotate(-1); return; }
    if (button == 2) { guiRotate(1); return; }
    if (button == 3) { guiBack(); return; } // retained as a convenient physical Back key
    if (button == 0) guiEnter();
}

void guiDraw() {
    static int lastVoices = -1, lastVolume = -1, lastLimit = -1;
    static bool lastSustain = false;
    static uint32_t lastUnderruns = UINT32_MAX;
    const int voices = Sampler.getActiveVoices();
    const int volume = int(Sampler.getMasterVolume() * 100 + 0.5f);
    const int limit = Sampler.getVoiceLimit();
    const bool sustain = Sampler.getSustain();
    const uint32_t underruns = Sampler.getUnderruns();
    if (!guiDirty && voices == lastVoices && volume == lastVolume && limit == lastLimit &&
        sustain == lastSustain && underruns == lastUnderruns) return;
    lastVoices = voices; lastVolume = volume; lastLimit = limit;
    lastSustain = sustain; lastUnderruns = underruns;
    guiDirty = false;
    guiScreen.clear();
    char line[64];
    // Persistent status bar; pages use the remaining seven text rows.
    snprintf(line, sizeof(line), "V%02d/%02d %3d%% SD%lu %s", voices, limit, volume,
             (unsigned long)underruns, sustain ? "P" : "-");
    guiScreen.text(0, line, true);
    const int folder = (guiPage == GUI_SETS) ? guiSelected : Sampler.getFolderId();
    const fname_t folderName = Sampler.getFolderName(folder);
    const str64_t title = Sampler.getTitle();
    if (guiPage == GUI_HOME) {
        guiScreen.text(2, !title.empty() ? title.c_str() : folderName.c_str());
        guiScreen.text(3, folderName.c_str());
        guiScreen.text(5, "Click: menu");
        guiScreen.text(7, "Hold: back / PANIC", true);
    } else if (guiPage == GUI_MENU) {
        guiScreen.text(2, "MAIN MENU");
        const char* const items[] = {"Sample set", "Master volume", "Effects", "Panic: all notes off"};
        for (int i=0; i<4; ++i) guiScreen.text(3+i, items[i], i == guiSelected);
    } else if (guiPage == GUI_SETS) {
        snprintf(line, sizeof(line), "SETS %d/%d", folder + 1, Sampler.getFolderCount());
        guiScreen.text(2, line);
        guiScreen.text(4, folderName.c_str(), true);
        guiScreen.text(5, folder == Sampler.getFolderId() ? "Current set" : "Click: load set");
        guiScreen.text(7, "Hold: back", true);
    } else if (guiPage == GUI_VOLUME) {
        guiScreen.text(2, "MASTER VOLUME");
        snprintf(line, sizeof(line), "%3d %%", volume); guiScreen.text(4, line, true);
        guiScreen.text(7, "Turn: adjust Hold:back", true);
    } else if (guiPage == GUI_FX_MENU) {
        guiScreen.text(2, "EFFECTS");
        for (int s=0; s<2; ++s) { snprintf(line, sizeof(line), "Slot %d: %s", s+1, guiFxName(Effects.type(s))); guiScreen.text(4+s, line, s == guiSelected); }
        guiScreen.text(7, "Click: open Hold:back", true);
    } else if (guiPage == GUI_FX_SLOT) {
        const RdxFxId id = Effects.type(guiFxSlot);
        snprintf(line, sizeof(line), "FX SLOT %d", guiFxSlot + 1); guiScreen.text(2, line);
        snprintf(line, sizeof(line), "Type: %s", guiFxName(id)); guiScreen.text(4, line, guiSelected == 0);
        snprintf(line, sizeof(line), "%s: %3u", guiFxParamName(id, 0), unsigned(Effects.param(guiFxSlot, 0))); guiScreen.text(5, line, guiSelected == 1);
        snprintf(line, sizeof(line), "%s: %3u", guiFxParamName(id, 1), unsigned(Effects.param(guiFxSlot, 1))); guiScreen.text(6, line, guiSelected == 2);
        guiScreen.text(7, "Click: edit Hold:back", true);
    } else { // GUI_FX_EDIT
        const RdxFxId id = Effects.type(guiFxSlot);
        snprintf(line, sizeof(line), "S%d %s", guiFxSlot + 1, guiFxField == 0 ? "TYPE" : guiFxParamName(id, guiFxField - 1)); guiScreen.text(2, line);
        if (guiFxField == 0) snprintf(line, sizeof(line), "%s", guiFxName(id));
        else snprintf(line, sizeof(line), "%3u", unsigned(Effects.param(guiFxSlot, guiFxField - 1)));
        guiScreen.text(4, line, true);
        guiScreen.text(7, "Turn: change Click:ok", true);
    }
    guiScreen.present();
    guiFrameReady = false;
}
}

void guiBegin() {
    for (uint8_t pin : guiButtonPins) pinMode(pin, INPUT_PULLUP);
    pinMode(GUI_ENC_A, INPUT_PULLUP); pinMode(GUI_ENC_B, INPUT_PULLUP);
    guiEncoder.begin(guiEncoderPins());
    guiSelected = guiObservedFolder = Sampler.getFolderId();
    guiLastRecovery = millis();
    guiOnline = guiInitDisplay();
    ESP_LOGI("GUI", "Core%d SH1106 %s, SDA=%d SCL=%d", xPortGetCoreID(),
             guiOnline ? "ready" : "absent; display disabled", GUI_SDA, GUI_SCL);
}

void guiPoll() {
    const uint32_t now = micros();
    if (uint32_t(now - guiLastPoll) < 1000) return;
    guiLastPoll = now;
    const int current = Sampler.getFolderId();
    if (current != guiObservedFolder) {
        guiObservedFolder = guiSelected = current;
        if (guiPage == GUI_SETS) guiPage = GUI_HOME;
        guiDirty = true;
    }
    const int rotation = guiEncoder.poll(guiEncoderPins()) * GUI_ENCODER_DIRECTION;
    if (rotation) {
        ++guiEncoderSteps;
        guiRotate(rotation);
    }
    const uint32_t nowMs = millis();
    for (unsigned i = 0; i < 5; ++i)
        guiClick(i, guiButtons[i].poll(digitalRead(guiButtonPins[i]) == LOW, nowMs));
    // The engine/control task owns all mutable state. Input remains responsive;
    // SD refill always wins and OLED traffic is throttled during playback.
    if (!guiOnline) {
        // Reset a failed driver/bus only during silence, never in an active
        // voice's refill deadline. Rate-limit retries for unplugged hardware.
        if (Sampler.getActiveVoices() == 0 && uint32_t(nowMs - guiLastRecovery) >= 5000) {
            guiLastRecovery = nowMs;
            Wire.end();
            guiOnline = guiInitDisplay();
            if (guiOnline) ++guiRecoveries;
        }
        return;
    }
    if (Sampler.needsRefill()) return;
    if (guiFrameReady && !guiVisible) {
        const uint8_t on = 0xaf;
        guiOnline = guiSend(0, &on, 1);
        guiVisible = guiOnline;
        if (!guiOnline) guiLastRecovery = nowMs;
        return;
    }
    if (!guiScreen.pending() && uint32_t(now - guiLastDraw) >= 100000) {
        guiLastDraw = now;
        guiDraw();
        return; // draw and transfer get separate control-loop passes
    }
    // A step transfers one 16-byte tile. With active voices, leave a wide gap
    // between tiles so Core1 can service MIDI and refill every voice first.
    const uint32_t transferInterval = Sampler.getActiveVoices() ? 5000 : 1000;
    if (guiScreen.pending() && uint32_t(now - guiLastTransfer) >= transferInterval) {
        const uint32_t began = micros();
        guiOnline = guiScreen.step(guiSend);
        if (!guiOnline) guiLastRecovery = millis();
        else if (!guiScreen.pending()) guiFrameReady = true;
        guiMaxTransferUs = max(guiMaxTransferUs, uint32_t(micros() - began));
        guiLastTransfer = micros();
    }
}

void guiPrintStats() {
    ESP_LOGI("GUI", "online=%d max_chunk_us=%lu errors=%lu wire_error=%u recoveries=%lu",
             guiOnline, (unsigned long)guiMaxTransferUs, (unsigned long)guiErrors,
             unsigned(guiLastWireError), (unsigned long)guiRecoveries);
    ESP_LOGI("GUI", "enc_steps=%lu btn_events=%lu page=%u selected=%d",
             (unsigned long)guiEncoderSteps, (unsigned long)guiButtonEvents, unsigned(guiPage), guiSelected + 1);
}
#else
void guiBegin() {}
void guiPoll() {}
void guiPrintStats() {}
#endif
