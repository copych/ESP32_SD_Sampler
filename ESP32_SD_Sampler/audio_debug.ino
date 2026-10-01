// Called only by ControlTask's silence-only report. Cumulative since reboot.
void printAudioDiagnostics() {
#if DEBUG_AUDIO_DIAGNOSTICS
  using namespace audio_debug;
  ESP_LOGI("AUDIO", "starts=%lu start_peak_ppm=%lu eof=%lu eof_peak_ppm=%lu env_end=%lu",
    (unsigned long)audio.count(START), (unsigned long)audio.peak(START),
    (unsigned long)audio.count(EOF_STOP), (unsigned long)audio.peak(EOF_STOP),
    (unsigned long)audio.count(ENVELOPE_END));
  ESP_LOGI("AUDIO", "force_stop=%lu stop_peak_ppm=%lu release=%lu release_peak_ppm=%lu fast_release=%lu fast_peak_ppm=%lu control_holds=%lu hold_peak_ppm=%lu",
    (unsigned long)control.count(FORCE_STOP), (unsigned long)control.peak(FORCE_STOP),
    (unsigned long)control.count(RELEASE), (unsigned long)control.peak(RELEASE),
    (unsigned long)control.count(FAST_RELEASE), (unsigned long)control.peak(FAST_RELEASE),
    (unsigned long)audio.count(PAUSE_GAP), (unsigned long)audio.peak(PAUSE_GAP));
  ESP_LOGI("AUDIO", "swaps=%lu seam_errors=%lu underruns=%lu read_errors=%lu",
    (unsigned long)audio.count(BUFFER_SWAP), (unsigned long)audio.count(SEAM_ERROR),
    (unsigned long)audio.count(UNDERRUN), (unsigned long)control.count(READ_ERROR));
  ESP_LOGI("AUDIO", "pause_samples=%lu pause_max_samples=%lu pause_gt1=%lu resume_jump_peak_ppm=%lu",
    (unsigned long)audio.count(PAUSE_SAMPLES), (unsigned long)audio.peak(PAUSE_SAMPLES),
    (unsigned long)audio.count(PAUSE_GT1), (unsigned long)audio.peak(RESUME_JUMP));
  ESP_LOGI("AUDIO", "release_jumps=%lu release_jump_peak_ppm=%lu",
    (unsigned long)audio.count(RELEASE_JUMP), (unsigned long)audio.peak(RELEASE_JUMP));
  ESP_LOGI("AUDIO", "clip_frames=%lu mix_peak_ppm=%lu nonfinite=%lu edge_events15=%lu edge_peak_ppm=%lu jump_blocks50=%lu",
    (unsigned long)audio.count(MIX_CLIP), (unsigned long)audio.peak(MIX_CLIP),
    (unsigned long)audio.count(MIX_NONFINITE), (unsigned long)audio.count(EDGE_EVENT),
    (unsigned long)audio.peak(EDGE_EVENT), (unsigned long)audio.count(OUTPUT_JUMP));
  ESP_LOGI("AUDIO", "compute_max_us=%lu slow_blocks=%lu i2s_errors=%lu control_gaps=%lu control_max_us=%lu dropped_events=%lu/%lu",
    (unsigned long)audio.peak(COMPUTE_US), (unsigned long)audio.count(SLOW_BLOCK),
    (unsigned long)audio.count(I2S_ERROR), (unsigned long)control.count(CONTROL_GAP_US),
    (unsigned long)control.peak(CONTROL_GAP_US), (unsigned long)audio.dropped.load(), (unsigned long)control.dropped.load());
  for (Recorder* recorder : {&audio, &control}) {
    Event event;
    for (unsigned n = 0; n < 8 && recorder->pop(event); ++n)
      ESP_LOGI("AEVENT", "%s t_ms=%lu kind=%s voice=%u note=%u pos=%lu value=%lu",
        recorder == &audio ? "audio" : "control", (unsigned long)event.timeMs,
        name(Kind(event.kind)), unsigned(event.voice), unsigned(event.note),
        (unsigned long)event.frame, (unsigned long)event.value);
  }
#endif
}
