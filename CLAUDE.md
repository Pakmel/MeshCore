# Badboj – MeshCore temperatursensor med kanalpush

## Vad detta är

En flytande badtermometer for Vanern/Karlstad. En RAK4631 på RAK19007 basplatta laser
vattentemperatur med en DS18B20 och pushar värdet en gång i timmen som ett vanligt
krypterat kanalmeddelande genom KSD MeshCore natet. Noden sover djupt mellan sandningarna.

Detta ar en fork av meshcore-dev/MeshCore. Basen ar exemplet `examples/simple_sensor`,
kopierat till ett eget exempel `examples/badboj_sensor` sa att uppstroms merges forblir rena.

## Arkitekturbeslut (andras inte utan diskussion)

* **Push, inte pull.** Standard MeshCore sensor ar pullbaserad och kraver konstant
  mottagning (8 till 10 mA). Denna nod pushar och sover, mal under 0,5 mA i snitt.
* **Kanalmeddelande som text.** Payload ar ett vanligt PAYLOAD_TYPE_GRP_TXT paket pa en
  privat kanal. Ingen egen payloaddesign, ingen specialavkodning. Meddelandet ska vara
  lasbart i MeshCore appen och i MeshMonitor kanalflodet.
* **Meddelandeformat, exakt:** `Badtemp: 18.5C Batt: 3.91V`
  Temperatur med en decimal, spanning med tva decimaler. Formatet ar ett kontrakt,
  parsning sker nedstroms i Home Assistant/MeshMonitor. Andra aldrig formatet i en patch.
* **Retry via egen eko.** Efter sandning: lyssna RETRY_WINDOW_S sekunder efter att en
  repeater studsar vart eget paket (matcha pa packet hash). Hors ingen repeat: sand om
  exakt en gang. Sedan somn oavsett utfall. Dubbletter i kanalen ar accepterat.
* **System ON sleep, inte SYSTEMOFF.** RTC maste overleva somnen sa att tidsstamplar
  och intervall haller. Vackning via RTC timer.
* **Ingen fjarradministration.** Konfigandring kraver USB. Alla parametrar,
  inklusive kanalnamnet, ar kompileringstidskonstanter i `src/badboj_config.h`.

## Hardvara

* RAK4631 (nRF52840 + SX1262) pa RAK19007 basplatta
* DS18B20 vattentat prob: VDD 3,3V, GND, data till WB_IO1, 4,7k pullup data till VDD
* DS18B20 kors i 9 bitars upplosning (0,5 C steg, under 100 ms konvertering).
  Konverteringen far inte blockera mesh loopen, starta asynkront eller sov under vantan.
* Batterispanning via board.getBattMilliVolts() (finns redan i SensorMesh)
* 4 st solpaneler parallellt (ca 30 mA styck verifierat matt, ej annonsens 300 mA)
  in pa P1 solkontakten. Laddintervall 4,4 till 5,5 V, Schottkydiod per panel.

## Radio

Samma preset som KSD natet i Karlstad (EU/UK). Vardena laggs i badboj_config.h och
verifieras mot en befintlig KSD nod fore forsta fardtest.

**Kanal: hashtagkanal.** Kanalnamnet (t.ex. `#badtemp`) ar en kompileringstidskonstant
i badboj_config.h. Nyckeln harledas vid boot som de forsta 16 byten av SHA256 pa hela
namnet inklusive #, exakt samma harledning som MeshCore apparna gor. Ingen secrets fil
behovs, namnet ar nyckeln. Verifiera harledningen mot ett kant exempel:
`#test` ska ge nyckeln `9cd8fcf22a47333b591d96a2b848b73f`. Anvand MeshCores egen
SHA256 hjalpfunktion, dra inte in ett nytt kryptobibliotek.

Medvetet vagval: hashtagkanaler ar publika by design, vem som helst med namnet kan
lasa och skriva. Accepterat for badtemperatur. Byt ALDRIG till denna kanaltyp for
nagot kansligt i andra projekt.

Duty cycle: en sandning i timmen plus max en retry ligger langt under 10 procent
pa 869 MHz. Retryfonstret far aldrig trigga mer an en omsandning.

## Versionering

* Enda kalla: `src/badboj_version.h` med `#define BADBOJ_VERSION "0.1.0"`
* Patch: bugfix utan beteendeandring. Minor: ny funktion. Major: format eller
  protokollandring (t.ex. andrat meddelandeformat). Ingen automatisk rollover.
* Versionen skrivs till serial vid boot och ingar INTE i kanalmeddelandet.
* Bygget maste ga igenom med noll fel och noll varningar i PlatformIO fore varje
  versionsbump. CLAUDE.md uppdateras i samma commit som koden.

## Byggmiljo

* PlatformIO i VS Code. RAK4631 targets kraver RAKs board support patch enligt
  RAK Wireless guide "How to Perform Installation of Board Support Package in
  PlatformIO" innan forsta bygget. Detta ar ett kant krav, inte ett fel.
* Nya lib_deps for detta exempel: paulstoffregen/OneWire, milesburton/DallasTemperature
* Flashning: dubbeltryck reset for bootloader, kopiera UF2

## Kanda fallgropar

* WB_IO2 styr 3,3V matning till vissa WisBlock moduler, anvand den inte till 1Wire.
* Firmware 1.11+ har en kand bugg dar RAK4631 rapporterat CPU temperatur i stallet
  for sensortemperatur i pull telemetrin. Var push laser DS18B20 direkt och beror
  inte pa den kodvagen, men verifiera alltid forsta avlasningen mot referens.
* Radion maste vara i RX under retryfonstret men i sleep/idle resten av timmen.
  Verifiera med strommatning, inte antaganden.

## Statusdefinition for "klar"

Noden ligger i vatten, skickar korrekt temperatur varje hel timme till kanalen,
syns i MeshMonitor via minst en KSD repeater, och snittforbrukningen ar matt
under 0,5 mA over ett dygn.
