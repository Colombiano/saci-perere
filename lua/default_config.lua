-- Ontologia em Lua: politicas declarativas do pipeline saci.
-- Este script RETORNA a tabela de politicas, que preenche saci::Config
-- (ver docs/ONTOLOGY.md e include/saci/config.hpp). Sem Lua compilado,
-- um arquivo "chave = valor" com as mesmas chaves planas serve de fallback
-- (mas sem a tabela `sites` — politica por-site e privilegio de Lua).

return {
  -- midia
  max_height       = 360,          -- maior degrau <= 360p
  player           = "mpv",

  -- legenda / traducao
  sub_lang_pref    = "en.*",       -- regex de idiomas aceitos na busca
  source_lang      = "en",         -- lingua da legenda de origem
  target_lang      = "pt-BR",      -- destino da narracao.
                                   -- ROADMAP DE IDIOMAS: nas proximas
                                   -- versoes o mesmo pipeline atende "es"
                                   -- (espanhol) e "zh" (chines).

  -- tts (Piper)
  tts_bin          = "piper",
  tts_voice        = "pt_BR-faber-medium.onnx",

  -- traducao
  translate_cmd    = "argos-translate",
  -- v0.3: backend "argos" (offline) ou "qwen" (LLM open source chines,
  -- Apache 2.0, roda local e gratuito via Ollama):
  --   translate_backend = "qwen",
  --   llm_cmd           = "ollama",
  --   llm_model         = "qwen2.5",   -- rode uma vez: ollama pull qwen2.5
  translate_backend = "argos",
  llm_cmd           = "ollama",
  llm_model         = "qwen2.5",

  -- v0.3: re-spawn do mux em modo fifo (quedas de rede). 0 = falha rapida.
  mux_retries      = 3,

  -- sincronia: LIMITES DE NATURALIDADE (invariantes do SyncFitter)
  tempo_min        = 0.85,
  tempo_max        = 1.30,
  drift_threshold_ms = 300,

  -- rede / disco
  ring_bytes       = 50 * 1024 * 1024,
  fifo_mode        = false,        -- true = sobrevive a quedas (fila em disco)

  -- politicas por-site/per-canal: a PRIMEIRA regra que casar com a URL
  -- vence (substring simples). Campo ausente/zero = herda o global.
  -- Exemplos comentados para nao surpreender ninguem:
  sites = {
    -- { pattern = "youtube.com", max_height = 360 },
    -- canal especifico merece voz propria:
    -- { pattern = "youtube.com/@CanalDeAulas", tts_voice = "pt_BR-faber-medium.onnx" },
    -- aula em espanhol? quando o roadmap de idiomas aterrizar:
    -- { pattern = "example.com/aulas-es", source_lang = "es", target_lang = "pt-BR" },
  },
}
