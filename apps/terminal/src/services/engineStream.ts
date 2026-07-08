import { useStore } from '../store'
import { ceriousWsBase } from '../platform/transport'
import type { Asset, ExecutionPosition, ExecutionRisk, WsMsg } from '../types'

const CONFIGURED_WS_BASE = (import.meta.env.VITE_CERIOUS_WS_BASE as string | undefined)?.trim()
const WS_BASE = CONFIGURED_WS_BASE || ceriousWsBase()
const WORKSPACE_SESSION_TOKEN_KEY = 'cerious.workspace.sessionToken.v1'
const ENABLE_LEGACY_BROWSER_WS = import.meta.env.VITE_CERIOUS_ENABLE_LEGACY_WS === 'true'

let installed = false
let metricsTimer: ReturnType<typeof setInterval> | undefined
let ws: WebSocket | undefined
let wsRetry: ReturnType<typeof setTimeout> | undefined
let currentStreamKey = ''

function workspaceToken(): string {
  try {
    return window.localStorage.getItem(WORKSPACE_SESSION_TOKEN_KEY) || ''
  } catch {
    return ''
  }
}

function applyEngineMessage(raw: unknown) {
  const store = useStore.getState()
  const msg = raw as Record<string, unknown>

  if (msg.type === 'snapshot') {
    store.loadSnapshot(msg.asset as Asset, msg)
    if (Array.isArray(msg.settlements) && msg.settlements.length > 0) {
      store.setSettlements(msg.settlements)
    }
    return
  }

  const typed = msg as WsMsg
  switch (typed.type) {
    case 'bar': store.pushBar(typed.asset, typed.data); break
    case 'bands': store.setBands(typed.asset, typed.data); break
    case 'book': store.setBook(typed.asset, typed.data); break
    case 'tick': store.pushTick(typed.asset, typed.data); break
    case 'zscore': store.setZscore(typed.asset, typed.value, typed.regime); break
    case 'signal': store.pushSignal(typed.data); break
    case 'position': store.setPositions(typed.data); break
    case 'metrics': store.setMetrics(typed.data); break
    case 'copy_status': store.setCopyStatus(typed.data); break
    case 'markets': store.setMarkets(typed.data); break
    case 'settlements': store.setSettlements(typed.data); break
    case 'poly_book': store.setPolyBook(typed.market_key, typed.data); break
    case 'poly_tick': store.pushPolyTick(typed.market_key, typed.data); break
    case 'fill': store.pushPolyFill(typed.market_key, typed.data); break
    case 'order_snapshot': store.setSimTradingState(typed.data); break
    case 'execution_event':
      if (Array.isArray(typed.data?.positions)) {
        if (typed.data.risk) store.setExecutionRisk(typed.data.risk as ExecutionRisk)
        store.setExecutionPositions(typed.data.positions as ExecutionPosition[])
      }
      break
  }
}

function connectLegacyBrowserStream() {
  if (!ENABLE_LEGACY_BROWSER_WS) return
  const { activeAsset, marketProvider } = useStore.getState()
  const streamKey = `${activeAsset}:${marketProvider}`
  if (streamKey === currentStreamKey && ws && ws.readyState <= WebSocket.OPEN) return
  currentStreamKey = streamKey
  window.clearTimeout(wsRetry)
  ws?.close()

  const params = new URLSearchParams({ provider: marketProvider })
  const token = workspaceToken()
  if (token) params.set('token', token)
  ws = new WebSocket(`${WS_BASE}/${activeAsset}?${params.toString()}`)
  ws.onmessage = event => {
    try {
      applyEngineMessage(JSON.parse(event.data))
    } catch {
      // Bad payloads are discarded; the C++ gateway remains the authority.
    }
  }
  ws.onclose = () => {
    wsRetry = window.setTimeout(connectLegacyBrowserStream, 2000)
  }
  ws.onerror = () => ws?.close()
}

async function pollMetrics() {
  try {
    const response = await fetch('/api/metrics')
    if (!response.ok) return
    useStore.getState().setMetrics(await response.json())
  } catch {
    // Metrics polling is advisory; core trading state comes from the C++ gateway.
  }
}

export function installEngineStream() {
  if (installed) return
  installed = true

  connectLegacyBrowserStream()
  useStore.subscribe((state, previous) => {
    if (state.activeAsset !== previous.activeAsset || state.marketProvider !== previous.marketProvider) {
      connectLegacyBrowserStream()
    }
  })

  void pollMetrics()
  metricsTimer = window.setInterval(() => void pollMetrics(), 5000)
}

export function stopEngineStreamForTests() {
  window.clearInterval(metricsTimer)
  window.clearTimeout(wsRetry)
  ws?.close()
  metricsTimer = undefined
  wsRetry = undefined
  ws = undefined
  installed = false
  currentStreamKey = ''
}
