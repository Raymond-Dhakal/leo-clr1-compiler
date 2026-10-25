# Leo project Q&A (RAG)

This folder is the start of a read-only assistant that answers questions using
Leo's own documentation and source files. The indexer and initial retrieval
use only Python's standard library, so no API key or extra package is needed to
build and search the local index.

## Build the local index

From the repository root:

```sh
python3 -m rag.indexer
```

This creates `rag/data/index.json` from the README, language reference, docs,
compiler source, headers, examples, and frontend text. The generated index is
local data and should not be committed.

## Search project material

```sh
python3 -c 'from rag.retrieval import search; print(search("how does the parser build LR states?"))'
```

## Run the app

The browser UI and API are served together on port 8000. Install the official
Groq Python client and create a `.env` file in the project root:

```sh
python3 -m pip install -r requirements.txt
```

```env
GROQ_API_KEY=your_groq_api_key
GROQ_MODEL=openai/gpt-oss-20b
```

Then run this from the project root:

```sh
make serve
```

Open `http://localhost:8000/ask.html` to ask questions. The key stays in the
backend process and is never sent to the browser. You can choose another
Groq-supported chat model with `GROQ_MODEL`; the default is
`openai/gpt-oss-20b`.

The assistant is read-only: it searches project files, sends the relevant
passages to Groq, and returns an answer with file and line citations.
