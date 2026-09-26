-- Ontologia em Lua: politicas declarativas do pipeline saci.
-- Mapeia 1:1 para saci::Config (ver docs/ONTOLOGY.md).

return {
  -- midia
  max_height       = 360,          -- maior degrau <= 360p
  player           = "mpv",

  -- legenda / traducao
  sub_lang_pref    = "en.*",       -- regex de idiomas aceitos na busca
  target_lang      = "pt-BR",

  -- tts (Piper)
  tts_bin          = "piper",
  tts_voice        = "pt_BR-faber-medium.onnx",

  -- traducao
  translate_cmd    = "argos-translate",

  -- sincronia: LIMITES DE NATURALIDADE (invariantes do SyncFitter)
  tempo_min        = 0.85,
  tempo_max        = 1.30,
  drift_threshold_ms = 300,

  -- rede / disco
  ring_bytes       = 50 * 1024 * 1024,
  fifo_mode        = false,        -- true = sobrevive a quedas (fila em disco)
}
