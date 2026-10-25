const form = document.getElementById('ask-form');
const question = document.getElementById('question');
const conversation = document.getElementById('conversation');
const status = document.getElementById('ask-status');
const submit = document.getElementById('ask-submit');

function inlineMarkdown(parent, text) {
  const pattern = /(\[([^\]]+)\]\((https?:\/\/[^\s)]+)\))|(`([^`]+)`)|(\*\*([^*]+)\*\*)|(__([^_]+)__)|(\*([^*\n]+)\*)|(_([^_\n]+)_)/g;
  let cursor = 0, match;
  while ((match = pattern.exec(text))) {
    parent.append(document.createTextNode(text.slice(cursor, match.index)));
    if (match[2] !== undefined) {
      const link = document.createElement('a');
      link.href = match[3];
      link.target = '_blank';
      link.rel = 'noopener noreferrer';
      inlineMarkdown(link, match[2]);
      parent.append(link);
    } else if (match[5] !== undefined) {
      const code = document.createElement('code');
      code.textContent = match[5];
      parent.append(code);
    } else if (match[7] !== undefined || match[9] !== undefined) {
      const strong = document.createElement('strong');
      inlineMarkdown(strong, match[7] ?? match[9]);
      parent.append(strong);
    } else {
      const emphasis = document.createElement('em');
      inlineMarkdown(emphasis, match[11] ?? match[13]);
      parent.append(emphasis);
    }
    cursor = pattern.lastIndex;
  }
  parent.append(document.createTextNode(text.slice(cursor)));
}

function renderMarkdown(text) {
  const fragment = document.createDocumentFragment();
  const lines = text.replace(/\r\n?/g, '\n').split('\n');
  let i = 0;
  const cells = (line) => line.trim().replace(/^\|/, '').replace(/\|$/, '').split('|').map((cell) => cell.trim());
  const isTableSeparator = (line) => /^\s*\|?\s*:?-{3,}:?\s*(\|\s*:?-{3,}:?\s*)+\|?\s*$/.test(line);
  const isBlockStart = (line, index) => /^(#{1,6}\s|```|>\s?|[-*+]\s+|\d+[.)]\s+|\s*([-*_]\s*){3,}$)/.test(line)
    || (index + 1 < lines.length && line.includes('|') && isTableSeparator(lines[index + 1]));

  while (i < lines.length) {
    const line = lines[i];
    if (!line.trim()) { i++; continue; }

    if (/^```/.test(line)) {
      i++;
      const codeLines = [];
      while (i < lines.length && !/^```/.test(lines[i])) codeLines.push(lines[i++]);
      if (i < lines.length) i++;
      const pre = document.createElement('pre');
      const code = document.createElement('code');
      code.textContent = codeLines.join('\n');
      pre.append(code);
      fragment.append(pre);
      continue;
    }

    const heading = line.match(/^(#{1,6})\s+(.*)$/);
    if (heading) {
      const element = document.createElement(`h${heading[1].length}`);
      inlineMarkdown(element, heading[2]);
      fragment.append(element);
      i++;
      continue;
    }

    if (/^\s*([-*_]\s*){3,}$/.test(line)) {
      fragment.append(document.createElement('hr'));
      i++;
      continue;
    }

    if (i + 1 < lines.length && line.includes('|') && isTableSeparator(lines[i + 1])) {
      const table = document.createElement('table');
      const head = document.createElement('thead');
      const headerRow = document.createElement('tr');
      for (const value of cells(line)) {
        const th = document.createElement('th');
        inlineMarkdown(th, value);
        headerRow.append(th);
      }
      head.append(headerRow);
      table.append(head);
      i += 2;
      const body = document.createElement('tbody');
      while (i < lines.length && lines[i].includes('|') && lines[i].trim()) {
        const row = document.createElement('tr');
        for (const value of cells(lines[i++])) {
          const td = document.createElement('td');
          inlineMarkdown(td, value);
          row.append(td);
        }
        body.append(row);
      }
      table.append(body);
      fragment.append(table);
      continue;
    }

    if (/^>\s?/.test(line)) {
      const quote = document.createElement('blockquote');
      while (i < lines.length && /^>\s?/.test(lines[i])) {
        const paragraph = document.createElement('p');
        inlineMarkdown(paragraph, lines[i++].replace(/^>\s?/, ''));
        quote.append(paragraph);
      }
      fragment.append(quote);
      continue;
    }

    const listMatch = line.match(/^\s*([-*+]\s+|\d+[.)]\s+)/);
    if (listMatch) {
      const ordered = /^\d/.test(listMatch[1]);
      const list = document.createElement(ordered ? 'ol' : 'ul');
      while (i < lines.length) {
        const item = lines[i].match(/^\s*([-*+]\s+|\d+[.)]\s+)(.*)$/);
        if (!item || /^\d/.test(item[1]) !== ordered) break;
        const li = document.createElement('li');
        inlineMarkdown(li, item[2]);
        list.append(li);
        i++;
      }
      fragment.append(list);
      continue;
    }

    const paragraph = document.createElement('p');
    const paragraphLines = [];
    while (i < lines.length && lines[i].trim() && !isBlockStart(lines[i], i)) paragraphLines.push(lines[i++]);
    if (!paragraphLines.length) paragraphLines.push(lines[i++]);
    inlineMarkdown(paragraph, paragraphLines.join(' '));
    fragment.append(paragraph);
  }
  return fragment;
}

function addTurn(questionText, answerText, sources = [], error = false) {
  const turn = document.createElement('article');
  turn.className = 'chat-turn';
  const prompt = document.createElement('div');
  prompt.className = 'chat-question';
  prompt.textContent = questionText;
  const answer = document.createElement('div');
  answer.className = 'chat-answer' + (error ? ' error' : '');
  if (error) answer.textContent = answerText;
  else answer.append(renderMarkdown(answerText));
  turn.append(prompt, answer);
  if (sources.length) {
    const sourceList = document.createElement('div');
    sourceList.className = 'sources';
    for (const source of sources) {
      const chip = document.createElement('span');
      chip.className = 'source-chip';
      chip.textContent = `[${source.number}] ${source.path}:${source.start_line}-${source.end_line}`;
      sourceList.append(chip);
    }
    turn.append(sourceList);
  }
  conversation.append(turn);
  turn.scrollIntoView({ behavior: 'smooth', block: 'nearest' });
}

document.querySelectorAll('.suggestion').forEach((button) => {
  button.addEventListener('click', () => {
    question.value = button.textContent;
    question.focus();
  });
});

fetch('/api/health').then((response) => response.json()).then((health) => {
  if (!health.groq_client_installed) status.textContent = 'Install the Groq client: python3 -m pip install -r requirements.txt';
  else if (!health.groq_configured) status.textContent = 'Add GROQ_API_KEY to the project .env file to enable answers.';
  else status.textContent = `Grounded in project files · ${health.model}`;
}).catch(() => { status.textContent = 'Could not connect to the RAG backend.'; });

form.addEventListener('submit', async (event) => {
  event.preventDefault();
  const prompt = question.value.trim();
  if (!prompt || submit.disabled) return;
  document.getElementById('welcome')?.remove();
  question.value = '';
  question.disabled = true;
  submit.disabled = true;
  status.textContent = 'Searching project files and asking Groq…';
  try {
    const response = await fetch('/api/ask', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ question: prompt }),
    });
    const result = await response.json();
    if (!response.ok) throw new Error(result.error || 'The request failed.');
    addTurn(prompt, result.answer, result.sources || []);
    status.textContent = `Answered using ${result.sources.length} project sources · ${result.model}`;
  } catch (error) {
    addTurn(prompt, error.message || 'Could not get an answer.', [], true);
    status.textContent = 'Answer failed. Check the backend status and try again.';
  } finally {
    question.disabled = false;
    submit.disabled = false;
    question.focus();
  }
});
