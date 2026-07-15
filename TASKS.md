# TASKS – Badboj firmware

Persistent sessionsminne for Claude Code. Bocka av med [x] och lagg anteckningar
under respektive punkt i samma commit som koden. Ta aldrig bort punkter, stryk dem.

## Runda 1 – Byggmiljo och baseline (inget eget kod an)

* [x] Forka meshcore-dev/MeshCore, klona lokalt, skapa branch `badboj`
      Fork: https://github.com/Pakmel/MeshCore, klonad 2026-07-15. Upstream main
      vid klontillfallet: commit 219812b9 (2026-07-13). Branch `badboj` skapad
      fran fork-main. CLAUDE.md och TASKS.md flyttade in i repots rot i samma
      commit. Notera: `C:\Users\pakme` ar sjalv en (troligen oavsiktlig) tom
      git-repo-rot utan commits, ej rord av detta arbete.
* [ ] Utfor RAKs BSP patch for PlatformIO enligt deras guide, dokumentera exakta
      steg har som anteckning (versioner, sokvagar) for reproducerbarhet
* [ ] Bygg stock `simple_sensor` for RAK4631 target med noll fel
* [ ] Flasha och verifiera boot over serial (115200), notera firmwareversion
* [ ] Kopiera examples/simple_sensor till examples/badboj_sensor, eget env i
      platformio.ini, bygg igen med noll fel
* [ ] Skapa src/badboj_version.h (0.1.0) och src/badboj_config.h med kanalnamn,
      intervall och retryfonster som konstanter

## Runda 2 – DS18B20 pa WB_IO1

* [ ] Lagg till OneWire och DallasTemperature i lib_deps for badboj_sensor
* [ ] Init av 1Wire buss pa WB_IO1 bakom build flag BADBOJ_DS18B20, 9 bitars
      upplosning, detektionskoll vid boot (flagga sätts bara om prob svarar)
* [ ] Asynkron avlasning: starta konvertering, hamta vardet utan att blockera
* [ ] Skriv temperatur och batterispanning till serial var 10:e sekund i testlage
* [ ] Verifiera mot referenstermometer i vattenglas, avvikelse under 1 C
* [ ] Version 0.2.0

## Runda 3 – Kanalpush

* [ ] Implementera nyckelharledning for hashtagkanal: forsta 16 byten av
      SHA256 pa kanalnamnet inkl #. Enhetstesta mot kant exempel:
      `#test` ska ge `9cd8fcf22a47333b591d96a2b848b73f`
* [ ] Satt kanalnamn i badboj_config.h, lagg in samma hashtagkanal i mobilappen
* [ ] Implementera sandning av PAYLOAD_TYPE_GRP_TXT med formatet
      `Badtemp: 18.5C Batt: 3.91V` (kontrakt, se CLAUDE.md)
* [ ] Testintervall 2 minuter, verifiera att meddelandet syns i appen via minst
      en repeater (inte bara direktlank)
* [ ] Verifiera i MeshMonitor kanalflodet
* [ ] Version 0.3.0

## Runda 4 – Retry via egen eko

* [ ] Efter TX: stanna i RX i RETRY_WINDOW_S (start 30 s), matcha inkommande
      paket mot eget packet hash
* [ ] Ingen repeat hord: sand om EN gang, sedan klart oavsett
* [ ] Logga utfall till serial: "repeat heard" / "retry sent" / "gave up"
* [ ] Testa genom att tillfalligt stanga av narmaste repeater och se retryn ga
* [ ] Version 0.4.0

## Runda 5 – Somncykel och strombudget

* [ ] RTC vackning varje hel timme (System ON sleep, RTC ska overleva)
* [ ] Sekvens: vakna, starta DS18B20 konvertering, las batteri, sand, retryfonster,
      sov. Total vakentid under 60 s per timme
* [ ] Verifiera att radion faktiskt ar nere mellan cykler (strommatning med
      multimeter eller PPK, mal under 0,5 mA i snitt over minst 6 h)
* [ ] Notera uppmatta varden har: sleep mA, RX mA, TX topp, snitt
* [ ] Version 0.5.0

## Runda 6 – Falttest fore sjosattning

* [ ] 48 h torrtest pa balkong pa enbart batteri, alla timsandningar mottagna
* [ ] Solpaneler inkopplade, verifiera laddning (rod LED / stigande spanning)
* [ ] Tomgangsspanning per panel matt i fullt solljus, under 5,5 V efter diod
* [ ] Sjosattning i badtemperaturmiljo, verifiera lank via KSD repeater
* [ ] Version 1.0.0 nar statusdefinitionen i CLAUDE.md ar uppfylld

## Parkerat / senare

* [ ] Parsning i Home Assistant till riktig sensorentitet (regex pa kanalmeddelande)
* [ ] Vintertest: islaggning, batteri i kyla
* [ ] Eventuell YouTube video nar 1.0.0 ar i vattnet
