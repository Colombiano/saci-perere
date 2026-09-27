-- Hooks do pipeline saci (v0.4) — OPCIONAL.
-- Ative em lua/default_config.lua:
--   hooks_file = "lua/hooks.lua"
--
-- on_stage(nome, ms_desde_a_etapa_anterior) e chamado a cada troca de
-- etapa do Orchestrator. Erros aqui NUNCA derrubam o pipeline — o saci
-- engole excecoes de hook de proposito (o show e do video, nao do hook).

function on_stage(nome, ms)
  -- Exemplo: linha de log simples. Faca o que quiser: contadores,
  -- arquivos, ate reagir a etapas especificas.
  print(string.format("[hook] etapa %-10s +%d ms", nome, ms))
end

-- Dica: para cronometrar so as etapas caras (TTS costuma dominar):
--
-- local acumulado = 0
-- function on_stage(nome, ms)
--   if nome == "Tts" then acumulado = acumulado + ms end
--   if nome == "Render" then
--     print(string.format("[hook] TTS total: %d ms", acumulado))
--   end
-- end
