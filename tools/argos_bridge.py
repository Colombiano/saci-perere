#!/usr/bin/env python3
"""Ponte de tradução em lote para o Saci Pererê (ArgosEngine, caminho rápido).

O CLI `argos-translate` traduz o stdin inteiro como UM parágrafo — a contagem
de linhas não bate com os segmentos do SRT e o saci caía no fallback lento
(um processo por segmento, ~4 s cada por recarregar o stanza). Esta ponte
carrega o modelo UMA vez e traduz N linhas na mesma proporção de sempre.

Uso:    printf 'linha1\nlinha2\n' | argos_bridge.py <from> <to>
Saída:  uma linha traduzida por linha de entrada, ordem preservada.
"""
import sys

import argostranslate.translate as translate


def main() -> int:
    if len(sys.argv) != 3:
        print("uso: argos_bridge.py <from> <to>", file=sys.stderr)
        return 64
    from_lang, to_lang = sys.argv[1], sys.argv[2]
    # Contrato 1:1 com o saci: UMA linha de saida por linha de entrada, na
    # mesma ordem — inclusive para linhas vazias. O saci compara a CONTAGEM
    # e cai no fallback lento (~4 s/segmento) se nao bater. Tambem nunca
    # deixar a traducao vazar '\n' (uma linha a mais ja quebra o contrato).
    texts = [line.rstrip("\n") for line in sys.stdin]
    for text in texts:
        if not text.strip():
            print()
            continue
        out = translate.translate(text, from_lang, to_lang)
        print(" ".join(out.splitlines()))
    return 0


if __name__ == "__main__":
    sys.exit(main())
