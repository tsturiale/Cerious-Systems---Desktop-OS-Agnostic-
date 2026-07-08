import { useEffect } from 'react'
import { Toaster } from 'react-hot-toast'
import { WorkspaceCanvas } from './components/WorkspaceCanvas'
import { ErrorBoundary } from './components/ErrorBoundary'
import { PortalGate } from './components/PortalGate'
import { useBroadcastSync } from './hooks/useBroadcastSync'
import { useStore } from './store'

/** Auto-rotates the active asset every 8 s when autoRotate is enabled.
 *  Only active in the primary window (no ?panel= param). */
function MarketRotator() {
  const { autoRotate, activeAsset, setActiveAsset, markets } = useStore()

  useEffect(() => {
    if (!autoRotate) return
    const assets = Array.from(new Set(markets.map(market => market.asset).filter(Boolean))) as typeof activeAsset[]
    if (!assets.length) return

    const id = setInterval(() => {
      const idx = assets.indexOf(activeAsset)
      const nextIdx = (idx + 1) % assets.length
      setActiveAsset(assets[nextIdx])
    }, 8000)

    return () => clearInterval(id)
  }, [autoRotate, activeAsset, markets, setActiveAsset])

  return null
}

/** Syncs asset/market/provider selection across all open windows. */
function BroadcastSyncProvider() {
  useBroadcastSync()
  return null
}

const toasterStyle = {
  background: '#1f242b',
  color: '#e8edf3',
  border: '1px solid #525a66',
  fontSize: '12px',
  fontFamily: 'Helvetica Neue, Helvetica, Arial, system-ui, sans-serif',
}

export default function App() {
  return (
    <ErrorBoundary>
      <PortalGate>
        <MarketRotator />
        <BroadcastSyncProvider />
        <WorkspaceCanvas />
      </PortalGate>
      <Toaster position="bottom-right" toastOptions={{ style: toasterStyle }} />
    </ErrorBoundary>
  )
}
