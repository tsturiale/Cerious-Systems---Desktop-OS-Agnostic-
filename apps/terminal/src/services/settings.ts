const SETTINGS_KEY = 'pt_settings_v1'

const THEMES = {
  'dark-navy': true,
  'deep-purple': true,
  'dark-midnight': true,
  'gunmetal': true,
  'abyss': true,
  'terminal': true,
} as const

export type ThemeKey = keyof typeof THEMES

export type TerminalSettings = {
  theme: ThemeKey
}

const DEFAULT_SETTINGS: TerminalSettings = {
  theme: 'dark-navy',
}

function isThemeKey(value: unknown): value is ThemeKey {
  return typeof value === 'string' && value in THEMES
}

export function loadSettings(): TerminalSettings {
  try {
    const raw = window.localStorage.getItem(SETTINGS_KEY)
    const parsed = raw ? JSON.parse(raw) as Partial<TerminalSettings> : {}
    return {
      theme: isThemeKey(parsed.theme) ? parsed.theme : DEFAULT_SETTINGS.theme,
    }
  } catch {
    return { ...DEFAULT_SETTINGS }
  }
}

export function applyTheme(key: ThemeKey) {
  document.documentElement.dataset.theme = key === 'dark-navy' ? '' : key
}
