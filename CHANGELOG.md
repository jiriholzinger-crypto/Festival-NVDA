# Changes

## 2026.11.5
- New dialog in the NVDA Tools menu (Festival voices) to download, install and remove voices from festvox.org, including the OALD dictionary for the British English voice rab_diphone.
- Downloaded voices are kept outside the add-on (in the NVDA user configuration, folder festivalVoices), so they survive add-on updates.
- Czech translation of the add-on and a Czech readme.

## 2026.11.4
- The default voice follows NVDA's language (czech_ph in Czech NVDA) instead of always the English kal_diphone.

## 2026.11.3
- Festival runs in the 64-bit helper process festhost64.exe, so a crash in Festival cannot close NVDA. The helper restarts automatically and writes crashes to festhost-crash.log.
- Speech is played by NVDA (output device, audio ducking, exact index reporting).

## 2026.11.2
- Rate 50 is each voice's natural tempo (the English voices kal and ked were 10% too fast); rate scale from half speed (0) to double speed (100).

## 2026.11.1
- New 64-bit festlib.dll 2.1 (Festival 2.5.1), pitch setting, capital pitch change.
