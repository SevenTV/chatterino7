# Theodors Chatterino7-fork – oppsett

Denne koden er basert på Chatterino7 og har allerede støtte for Twitch, Kick og **Multi**, som viser flere kanaler i samme chatfane.

## Bruke Twitch og Kick i samme fane

1. Start programmet og logg inn på Twitch under **Settings → Accounts**.
2. Logg inn på Kick samme sted dersom du skal skrive i Kick-chatten.
3. Høyreklikk på en chatfane eller trykk på kanalvelgeren.
4. Åpne fanen **Multi**.
5. Trykk **Add**.
6. Velg **Twitch**, skriv kanalnavnet og legg den til.
7. Trykk **Add** igjen, velg **Kick**, og legg til Kick-kanalen.
8. Velg **Platform badge** som kanalindikator.
9. Trykk **OK**.

Begge chattene vises nå i samme fane. Plattformmerket viser hvor hver melding kommer fra. I skrivefeltet kan du velge hvilken underkanal meldingen skal sendes til.

## Lage GitHub-repository

Installer først GitHub CLI og logg inn med `gh auth login`. Skriptet oppretter en ekte fork av `SevenTV/chatterino7`, slik at Git-historikk og submodules blir beholdt.

Kjør deretter i PowerShell fra denne prosjektmappen:

```powershell
./scripts/setup-fork.ps1 -GitHubUsername "DITT_GITHUB_NAVN" -Repository "chatterino7-twitch-kick"
```

Skriptet forker og kloner originalprosjektet, kopierer inn endringene, oppretter `main`, committer og pusher til GitHub.

## Bygge på Windows 11

Den enkleste metoden er GitHub Actions:

1. Push prosjektet til GitHub.
2. Åpne fanen **Actions** i repositoryet.
3. Velg workflowen **Build**.
4. Trykk **Run workflow**.
5. Last ned Windows-artifacten når byggingen er ferdig.

For lokal bygging, følg `BUILDING_ON_WINDOWS.md`. Prosjektet krever blant annet Visual Studio 2022, CMake, Qt og prosjektets Git-submodules.

## Oppdatere fra original Chatterino7

```powershell
git fetch upstream
git checkout main
git merge upstream/chatterino7
```

Løs eventuelle konflikter, test, og push deretter:

```powershell
git push origin main
```

## Endringer i denne forken

- Twitch- og Kick-kanaler kan legges inn i samme **Multi**-fane.
- Plattformmerket er standardvalg og vises på alle meldinger.
- GitHub Actions bygger både `main` og den opprinnelige `chatterino7`-grenen.
- Norsk veiledning og et PowerShell-skript for opprettelse av fork er inkludert.

Prosjektet beholder den opprinnelige MIT-lisensen og krediteringen til Chatterino- og Chatterino7-bidragsyterne.
