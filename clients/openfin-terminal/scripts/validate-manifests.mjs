import fs from 'node:fs'
import path from 'node:path'
import { fileURLToPath } from 'node:url'

const dirname = path.dirname(fileURLToPath(import.meta.url))
const manifestDir = path.resolve(dirname, '..', 'manifests')
const manifests = fs.readdirSync(manifestDir).filter((name) => name.endsWith('.json'))

const forbidden = /\b(qt|tauri|chrome-app)\b/i
let failures = 0

for (const name of manifests) {
  const file = path.join(manifestDir, name)
  const text = fs.readFileSync(file, 'utf8')
  const manifest = JSON.parse(text)
  const app = manifest.startup_app

  const errors = []
  if (!app) errors.push('missing startup_app')
  if (app && !app.uuid) errors.push('missing startup_app.uuid')
  if (app && !app.name) errors.push('missing startup_app.name')
  if (app && !app.url) errors.push('missing startup_app.url')
  if (app && app.customData?.authority !== 'server') errors.push('customData.authority must be server')
  if (forbidden.test(text)) errors.push('contains retired desktop technology wording')

  if (errors.length) {
    failures += 1
    console.error(`${name}: ${errors.join('; ')}`)
  } else {
    console.log(`${name}: ok -> ${app.url}`)
  }
}

if (failures) {
  process.exit(1)
}
