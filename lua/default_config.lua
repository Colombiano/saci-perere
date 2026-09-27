-- Ontologia em Lua: politicas declarativas do pipeline saci.
-- Este script RETORNA a tabela de politicas, que preenche saci::Config
-- (ver docs/ONTOLOGY.md e include/saci/config.hpp). Sem Lua compilado,
-- um arquivo "chave = valor" com as mesmas chaves planas serve de fallback
-- (mas sem a tabela `sites` — politica por-site e privilegio de Lua).

return {
  -- midia
  max_height       = 360,          -- maior degrau <= 360p
  player           = "mpv",
  yt_dlp           = "yt-dlp",     -- binario do yt-dlp; versao recente!
                                   -- (2025.04 quebra com SABR; use 2025.09+)

  -- legenda / traducao
  sub_lang_pref    = "en",       -- regex de idiomas aceitos na busca
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
  -- v0.6: ponte de lote (tools/argos_bridge.py) via wrapper executavel.
  -- Carrega o modelo 1x p/ N segmentos; sem ela, o fallback e ~4s/segmento.
  -- translate_bridge = "/caminho/argos-bridge",
  -- v0.3: backend "argos" (offline) ou "qwen" (LLM open source chines,
  -- Apache 2.0 exceto 3B/72B — verifique o model card; roda local e
  -- gratuito via Ollama):
  --   translate_backend = "qwen",
  --   llm_cmd           = "ollama",
  --   llm_model         = "qwen2.5",   -- rode uma vez: ollama pull qwen2.5
  translate_backend = "argos",
  llm_cmd           = "ollama",
  llm_model         = "qwen2.5",

  -- v0.3: re-spawn do mux em modo fifo (quedas de rede). 0 = falha rapida.
  mux_retries      = 3,

  -- v0.4: escada de qualidade (degrau x kbps). Vazio/ausente = tabela
  -- consteval embutida (144/240/360/480/720p).
  -- ladder = {
  --   { height = 144, video_kbps = 80 },
  --   { height = 360, video_kbps = 700 },
  -- },

  -- v0.4: hooks Lua opcionais — script com on_stage(nome, ms) chamado a
  -- cada troca de etapa do pipeline. Exemplo pronto em lua/hooks.lua.
  hooks_file       = "",  -- ex.: "lua/hooks.lua"

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
