/**
 * useBroadcastSync — syncs user UI selections across all open terminal windows.
 *
 * Primary window (no ?panel= param): broadcasts activeAsset, activeMarketKey,
 *   and marketProvider whenever they change in the Zustand store.
 *
 * Secondary windows (?panel=X): listens on the same channel and updates their
 *   local store so all windows track the same active market without a shared WS.
 */
import { useEffect } from 'react'
import { useStore } from '../store'
import type { Asset, MarketProvider } from '../types'

const CHANNEL = 'qst-ui-sync'

type SyncMsg =
  | { type: 'asset';    value: string }
  | { type: 'market';   value: string | null }
  | { type: 'provider'; value: string }

const ASSET_VALUES: readonly Asset[] = ['ES', 'NQ', 'YM', 'RTY', 'CL', 'GC', 'ZM', 'ZS', 'ES_NQ', 'YM_ES', 'RTY_ES', 'BTC', 'ETH', 'SOL', 'XRP', 'HYPE', 'BNB', 'DOGE', 'EVENT']
const PROVIDER_VALUES: readonly MarketProvider[] = ['cme', 'polymarket', 'kalshi', 'forecasttrader', 'hyperliquid', 'coingecko']

function isAsset(value: string): value is Asset {
  return ASSET_VALUES.includes(value as Asset)
}

function isMarketProvider(value: string): value is MarketProvider {
  return PROVIDER_VALUES.includes(value as MarketProvider)
}

/** Returns true when this window is a panel popout (has ?panel= in URL). */
export function isPanelWindow(): boolean {
  return new URLSearchParams(window.location.search).has('panel')
}

/** Returns the panel type from the URL, or null if not a panel window. */
export function getPanelType(): string | null {
  return new URLSearchParams(window.location.search).get('panel')
}

/**
 * Mount in App.tsx.
 * Primary: subscribes to store, broadcasts changes to other windows.
 * Secondary: listens for broadcasts and applies them to the local store.
 */
export function useBroadcastSync() {
  const isSecondary = isPanelWindow()

  useEffect(() => {
    if (typeof BroadcastChannel === 'undefined') return
    const ch = new BroadcastChannel(CHANNEL)

    if (isSecondary) {
      // ── Secondary: receive and apply ──────────────────────────────────────
      ch.onmessage = (e: MessageEvent<SyncMsg>) => {
        const { setActiveAsset, setActiveMarketKey, setMarketProvider } = useStore.getState()
        const msg = e.data
        if (msg.type === 'asset' && isAsset(msg.value)) setActiveAsset(msg.value)
        if (msg.type === 'market')   setActiveMarketKey(msg.value)
        if (msg.type === 'provider' && isMarketProvider(msg.value)) setMarketProvider(msg.value)
      }
    } else {
      // ── Primary: subscribe to store and broadcast changes ─────────────────
      const prev = {
        asset:    useStore.getState().activeAsset,
        market:   useStore.getState().activeMarketKey,
        provider: useStore.getState().marketProvider,
      }

      const unsub = useStore.subscribe((state) => {
        if (state.activeAsset !== prev.asset) {
          prev.asset = state.activeAsset
          ch.postMessage({ type: 'asset', value: state.activeAsset } satisfies SyncMsg)
        }
        if (state.activeMarketKey !== prev.market) {
          prev.market = state.activeMarketKey
          ch.postMessage({ type: 'market', value: state.activeMarketKey } satisfies SyncMsg)
        }
        if (state.marketProvider !== prev.provider) {
          prev.provider = state.marketProvider
          ch.postMessage({ type: 'provider', value: state.marketProvider } satisfies SyncMsg)
        }
      })

      return () => {
        unsub()
        ch.close()
      }
    }

    return () => ch.close()
  }, [isSecondary])
}
