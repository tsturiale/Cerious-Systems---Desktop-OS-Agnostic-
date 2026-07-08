import { readFileSync, readdirSync, statSync } from 'node:fs'
import { join, relative } from 'node:path'

const root = process.cwd()
const srcRoot = join(root, 'apps', 'terminal', 'src')

const forbidden = [
  {
    name: 'hard-coded futures product math in React',
    pattern: /\b(FUTURES_CONTRACT_SPECS|futuresContractSpecFor|futuresPnl)\b/,
  },
  {
    name: 'client P&L total fallback',
    pattern: /\b(totalPnl|dayPnl)\s*:\s*[^\n]*(openPnl\s*\+|realizedPnl\s*\+|closedPnl\s*\+|[+]\s*openPnl|[+]\s*realizedPnl|[+]\s*closedPnl)/,
  },
  {
    name: 'client session drawdown authority',
    pattern: /\b(sessionLowPnl|sessionPeakPnl|maxDrawdown)\s*=\s*Math\./,
  },
]

const allowedOfflineBlocks = [
  {
    file: join('apps', 'terminal', 'src', 'components', 'WorkspaceCanvas.tsx'),
    start: 'function CeriousTradeAnalyticsWindow',
    end: 'function CeriousNotionalCalculatorWindow',
    reason: 'Trade Analytics widget rendering scale only; analytics authority is backend payload',
  },
]

function walk(dir) {
  const files = []
  for (const name of readdirSync(dir)) {
    const path = join(dir, name)
    const stat = statSync(path)
    if (stat.isDirectory()) files.push(...walk(path))
    else if (/\.(ts|tsx|js|jsx)$/.test(name)) files.push(path)
  }
  return files
}

function offlineRangesFor(file, text) {
  const rel = relative(root, file)
  return allowedOfflineBlocks
    .filter(block => block.file === rel)
    .flatMap(block => {
      const start = text.indexOf(block.start)
      const end = text.indexOf(block.end, start >= 0 ? start : 0)
      return start >= 0 && end > start ? [{ start, end, reason: block.reason }] : []
    })
}

function isAllowed(index, ranges) {
  return ranges.some(range => index >= range.start && index < range.end)
}

const violations = []
for (const file of walk(srcRoot)) {
  const text = readFileSync(file, 'utf8')
  const ranges = offlineRangesFor(file, text)
  for (const rule of forbidden) {
    const regex = new RegExp(rule.pattern.source, `${rule.pattern.flags}g`)
    let match
    while ((match = regex.exec(text)) !== null) {
      if (isAllowed(match.index, ranges)) continue
      const before = text.slice(0, match.index)
      const line = before.split(/\r?\n/).length
      violations.push(`${relative(root, file)}:${line} ${rule.name}`)
    }
  }
}

if (violations.length) {
  console.error('React dumb-terminal audit failed:')
  for (const violation of violations) console.error(`- ${violation}`)
  process.exit(1)
}

console.log('React dumb-terminal audit passed: no live React authority patterns found.')
