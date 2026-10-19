import i18n from 'i18next';
import { initReactI18next } from 'react-i18next';

import en from './locales/en.json';
import zh from './locales/zh.json';

export const LANGUAGES = [
  { id: 'zh', label: '简体中文' },
  { id: 'en', label: 'English' },
] as const;

export type LanguageId = (typeof LANGUAGES)[number]['id'];

const STORAGE_KEY = 'gitsight.lang';

/**
 * The console's own language is the same setting the report body uses
 * (`config.lang`), so a reader who switches to Chinese also gets Chinese export
 * headings. The stored preference wins on load; `applyConfigLanguage` lets the
 * config take over once it has been read, so the two never disagree.
 */
function initialLanguage(): LanguageId {
  const stored = readStored();
  if (stored) return stored;
  return navigator.language.toLowerCase().startsWith('zh') ? 'zh' : 'en';
}

function readStored(): LanguageId | undefined {
  try {
    const stored = localStorage.getItem(STORAGE_KEY);
    return stored === 'zh' || stored === 'en' ? stored : undefined;
  } catch {
    // Private mode can make localStorage throw; the browser language still works.
    return undefined;
  }
}

void i18n.use(initReactI18next).init({
  resources: {
    en: { translation: en },
    zh: { translation: zh },
  },
  lng: initialLanguage(),
  fallbackLng: 'en',
  interpolation: { escapeValue: false },
  // Without this a namespace lookup misses would silently render the raw key,
  // which is how untranslated strings end up looking like "preview.rowsTruncated".
  parseMissingKeyHandler: (key) => `⚠ ${key}`,
});

export function setLanguage(lang: LanguageId): void {
  void i18n.changeLanguage(lang);
  try {
    localStorage.setItem(STORAGE_KEY, lang);
  } catch {
    /* Not remembering the choice is better than failing to switch. */
  }
  document.documentElement.lang = lang;
}

/**
 * Mirrors the config's report language into the console's own.
 *
 * Settings writes `lang` and the header toggle changes the UI; pointing both at
 * one value is what keeps a reader from saving Chinese and still reading an
 * English console. An explicit choice in the header wins over the config, so
 * only an untouched preference is adopted here.
 */
export function applyConfigLanguage(lang: string | undefined): void {
  if (readStored() || !lang) return;
  const normalized: LanguageId = lang.toLowerCase().startsWith('zh') ? 'zh' : 'en';
  if (normalized !== i18n.language) setLanguage(normalized);
}

export default i18n;