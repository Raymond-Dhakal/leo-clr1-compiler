"""Dependency-free lexical retrieval for an initial RAG prototype."""

from __future__ import annotations

import json
import math
import re
from collections import Counter
from pathlib import Path
from typing import Any

from .indexer import INDEX_PATH, build_index


WORD_RE = re.compile(r"[a-zA-Z_][a-zA-Z_0-9]*|\d+")


def terms(text: str) -> list[str]:
    return [word.lower() for word in WORD_RE.findall(text)]


def _load_chunks(index_path: Path) -> list[dict[str, Any]]:
    if not index_path.exists():
        build_index(output=index_path)
    return json.loads(index_path.read_text(encoding="utf-8"))


def search(query: str, limit: int = 6, index_path: Path = INDEX_PATH) -> list[dict[str, Any]]:
    """Return the best matching chunks with BM25-style ranking and citations."""
    query_terms = terms(query)
    if not query_terms:
        return []

    chunks = _load_chunks(index_path)
    tokenized = [terms(chunk["text"]) for chunk in chunks]
    doc_freq: Counter[str] = Counter()
    for words in tokenized:
        doc_freq.update(set(words))
    avg_length = sum(map(len, tokenized)) / max(len(tokenized), 1)

    results: list[dict[str, Any]] = []
    for chunk, words in zip(chunks, tokenized):
        frequencies = Counter(words)
        score = 0.0
        for term in query_terms:
            frequency = frequencies[term]
            if not frequency:
                continue
            inverse_frequency = math.log(1 + (len(chunks) - doc_freq[term] + 0.5) / (doc_freq[term] + 0.5))
            denominator = frequency + 1.2 * (0.25 + 0.75 * len(words) / max(avg_length, 1))
            score += inverse_frequency * (frequency * 2.2 / denominator)
        if score > 0:
            results.append({**chunk, "score": round(score, 4)})

    results.sort(key=lambda result: (-result["score"], result["source"], result["start_line"]))
    return results[:max(1, limit)]
