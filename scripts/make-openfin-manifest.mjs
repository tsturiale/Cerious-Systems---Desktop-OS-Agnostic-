#!/usr/bin/env node
import fs from 'node:fs'
import path from 'node:path'
import { fileURLToPath } from 'node:url'

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..')
const profile = process.env.CERIOUS_DESKTOP_PROFILE || 'local'
const baseUrl = (process.env.CERIOUS_TERMINAL_URL || process.env.CERIOUS_GATEWAY_HTTP || 'http://127.0.0.1:8000').replace(/\/+$/, '')
const out = process.env.CERIOUS_OPENFIN_MANIFEST || path.join(root, 'clients', 'openfin-terminal', 'manifests', `${profile}.generated.json`)
const name = profile === 'local' ? 'Cerious Desktop' : `Cerious Desktop ${profile}`
const uuid = `cerious-systems-desktop-${profile}`

const manifest = {
  runtime: {
    version: 'stable',
  },
  startup_app: {
    uuid,
    name,
    url: `${baseUrl}/?cerious_client=openfin&cerious_desktop=launcher`,
    autoShow: false,
    defaultCentered: true,
    defaultWidth: 420,
    defaultHeight: 320,
    minWidth: 360,
    minHeight: 240,
    frame: true,
    resizable: true,
    maximizable: true,
    saveWindowState: false,
    showTaskbarIcon: true,
    waitForPageLoad: true,
    icon: `${baseUrl}/branding/cerious-logo.png`,
    customData: {
      ceriousClient: 'openfin-desktop-launcher',
      backendProfile: profile,
      gatewayBaseUrl: baseUrl,
      authority: 'server',
    },
  },
}

fs.mkdirSync(path.dirname(out), { recursive: true })
fs.writeFileSync(out, `${JSON.stringify(manifest, null, 2)}\n`)
console.log(out)
