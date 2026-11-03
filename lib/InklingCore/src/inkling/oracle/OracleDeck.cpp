#include "inkling/oracle/OracleDeck.h"

#include <cstdio>
#include <cstring>

namespace inkling {
namespace {

using freeink::ui::Color;
using freeink::ui::Paint;
using freeink::ui::Point;
using freeink::ui::Rect;
using freeink::ui::TextAlign;
using freeink::ui::TextStyle;
using namespace ui::canvas;

static const OracleCard kDailyStoicCards[] = {
    {
        "MARCO AURELIO",
        "Meditazioni, Libro IV",
        "Hai potere sulla tua mente, non sugli eventi esterni. Comprendi questo con chiarezza, e troverai la forza.",
        "MEMENTO: Concentrati unicamente sulle tue reazioni, mai sul caos del mondo.",
    },
    {
        "LUCIO ANNEO SENECA",
        "Lettere a Lucilio, 104",
        "Non e' perche' le cose sono difficili che non osiamo, e' perche' non osiamo che sono difficili.",
        "AZIONE: Comincia subito quel compito impegnativo che stai rimandando.",
    },
    {
        "EPITTETO",
        "Enchiridion, Capitolo V",
        "Cio' che turba gli uomini non sono le cose in se', ma i giudizi che essi formulano sulle cose.",
        "DISCIPLINA: Riformula l'ostacolo togliendo il filtro del risentimento.",
    },
    {
        "ITALO CALVINO",
        "Lezioni Americane",
        "Prendete la vita con leggerezza, che leggerezza non e' superficialita', ma planare sulle cose dall'alto, senza macigni sul cuore.",
        "RESPIRA: Sottrai peso superfluo ai tuoi pensieri e focalizzati sull'essenza.",
    },
    {
        "JORGE LUIS BORGES",
        "Poema dei Doni",
        "Ho sempre immaginato che il Paradiso fosse una specie di biblioteca infinita.",
        "RIFUGIO: Un'ora di lettura silenziosa e' uno scudo inviolabile contro il rumore.",
    },
    {
        "FRIEDRICH NIETZSCHE",
        "Crepuscolo degli Idoli",
        "Chi ha un perche' abbastanza forte per vivere, puo' sopportare quasi qualsiasi come.",
        "SCOPO: Riconnettiti con il motivo profondo per cui hai scelto questa strada.",
    },
    {
        "MARCO AURELIO",
        "Meditazioni, Libro V",
        "Quando ti svegli al mattino, pensa a quale privilegio prezioso sia essere vivi: respirare, pensare, provare gioia, amare.",
        "GRATITUDINE: Inizia la giornata ringraziando per l'aria che riempie i polmoni.",
    },
    {
        "LUCIO ANNEO SENECA",
        "Della brevita' della vita",
        "Mentre perdiamo il nostro tempo tra indugi, dubbi e rinvii, la vita se ne va inesorabile.",
        "PRESENTE: Il momento presente e' l'unica moneta che possiedi realmente.",
    },
    {
        "ARTHUR SCHOPENHAUER",
        "Parerga e Paralipomena",
        "La solitudine e' la sorte di tutti gli spiriti eccellenti. Chi non ama la solitudine non ama la liberta'.",
        "CALMA: Usa il silenzio della sera come uno scalpello per chiarire le tue idee.",
    },
    {
        "EPITTETO",
        "Manuale, Capitolo VIII",
        "Non pretendere che le cose avvengano come vuoi tu; desidera invece che avvengano come avvengono.",
        "AMOR FATI: Accogli l'imprevisto come il miglior maestro di oggi.",
    },
};

static const OracleCard kObliqueStrategiesCards[] = {
    {
        "BRIAN ENO & PETER SCHMIDT",
        "Oblique Strategies #01",
        "Honor thy error as a hidden intention.",
        "SBLOCCO: Un difetto o un errore inatteso e' la vera porta verso la novita'.",
    },
    {
        "BRIAN ENO & PETER SCHMIDT",
        "Oblique Strategies #02",
        "Use an old idea.",
        "SBLOCCO: Ripesca un bozzetto o un'intuizione archiviata mesi fa e ridagli vita.",
    },
    {
        "BRIAN ENO & PETER SCHMIDT",
        "Oblique Strategies #03",
        "Work at a different speed.",
        "SBLOCCO: Se stai correndo, rallenta a passo d'uomo. Se sei bloccato, fai uno sprint.",
    },
    {
        "BRIAN ENO & PETER SCHMIDT",
        "Oblique Strategies #04",
        "Look closely at the most embarrassing details and amplify them.",
        "SBLOCCO: Non nascondere l'imperfezione: ingigantiscila fino a renderla cifra stilistica.",
    },
    {
        "BRIAN ENO & PETER SCHMIDT",
        "Oblique Strategies #05",
        "State the problem in words as clearly as possible.",
        "SBLOCCO: Se non sai spiegarlo a parole in due frasi, non lo hai ancora messo a fuoco.",
    },
    {
        "BRIAN ENO & PETER SCHMIDT",
        "Oblique Strategies #06",
        "Do nothing for as long as possible.",
        "SBLOCCO: Stacca le mani dalla tastiera. Lascia decantare l'idea senza toccarla.",
    },
    {
        "BRIAN ENO & PETER SCHMIDT",
        "Oblique Strategies #07",
        "What would your closest friend do?",
        "SBLOCCO: Guarda la situazione con gli occhi di chi ti stima ma non ha i tuoi filtri.",
    },
    {
        "BRIAN ENO & PETER SCHMIDT",
        "Oblique Strategies #08",
        "Discard an axiom.",
        "SBLOCCO: Quale regola data per scontata puoi infrangere deliberatamente oggi?",
    },
    {
        "BRIAN ENO & PETER SCHMIDT",
        "Oblique Strategies #09",
        "Simple subtraction.",
        "SBLOCCO: Elimina l'elemento che ritieni piu' indispensabile e vedi se regge.",
    },
    {
        "BRIAN ENO & PETER SCHMIDT",
        "Oblique Strategies #10",
        "Turn it upside down.",
        "SBLOCCO: Ribalta le premesse. Se doveva essere bianco fallo nero; se pieno, vuoto.",
    },
    {
        "BRIAN ENO & PETER SCHMIDT",
        "Oblique Strategies #11",
        "Listen to the quiet voice.",
        "SBLOCCO: La primissima impressione sommessa che hai avuto era probabilmente quella giusta.",
    },
    {
        "BRIAN ENO & PETER SCHMIDT",
        "Oblique Strategies #12",
        "Are there sections? Consider transitions.",
        "SBLOCCO: La bellezza risiede nei punti di passaggio e nei contrasti tra le parti.",
    },
};

static constexpr int kStoicCount = sizeof(kDailyStoicCards) / sizeof(kDailyStoicCards[0]);
static constexpr int kEnoCount = sizeof(kObliqueStrategiesCards) / sizeof(kObliqueStrategiesCards[0]);

}  // namespace

OracleDeck::OracleDeck() = default;

void OracleDeck::toggleDeck() {
  deckType_ = (deckType_ == OracleDeckType::DailyStoic) ? OracleDeckType::ObliqueStrategies
                                                        : OracleDeckType::DailyStoic;
  currentIdx_ = 0;
  justShuffled_ = false;
}

int OracleDeck::totalCards() const {
  return (deckType_ == OracleDeckType::DailyStoic) ? kStoicCount : kEnoCount;
}

const OracleCard& OracleDeck::currentCard() const {
  if (deckType_ == OracleDeckType::DailyStoic) {
    return kDailyStoicCards[currentIdx_ % kStoicCount];
  }
  return kObliqueStrategiesCards[currentIdx_ % kEnoCount];
}

void OracleDeck::nextCard() {
  currentIdx_ = (currentIdx_ + 1) % totalCards();
  justShuffled_ = false;
}

void OracleDeck::prevCard() {
  currentIdx_ = (currentIdx_ + totalCards() - 1) % totalCards();
  justShuffled_ = false;
}

void OracleDeck::shuffle() {
  seed_ = seed_ * 1664525u + 1013904223u;
  currentIdx_ = static_cast<int>(seed_ % totalCards());
  justShuffled_ = true;
}

void OracleDeck::onShake() {
  shuffle();
}

void OracleDeck::render(freeink::ui::DrawTarget& t, const DeviceStatus& status) {
  (void)status;
  clearScreen();
  const int16_t w = kScreenW;
  const auto& card = currentCard();

  // Top HUD Bar
  drawPill(t, 14, 8, 22, "L'ORACOLO · PENSIERO & STRATEGIE", ui::kFontMicro, true, 10);
  char cardIdxPill[32];
  snprintf(cardIdxPill, sizeof(cardIdxPill), "CARTA #%02d / %02d", currentIdx_ + 1, totalCards());
  drawPill(t, static_cast<int16_t>(w - 110), 8, 22, cardIdxPill, ui::kFontMicro, false, 8);

  // Big Hero Oracle Card (y=40..736)
  const Rect frameRect{14, 40, static_cast<int16_t>(w - 28), 696};
  drawCard(t, frameRect, 12, 4, false, true);

  // Inner double border for an art-print literary aesthetic
  fastStroke(t, Rect{24, 50, static_cast<int16_t>(w - 48), 676}, Paint::solid(Color::Black), 1, 8);

  // Deck Mode Header Pill inside card
  const char* deckTag = (deckType_ == OracleDeckType::DailyStoic) ? "DAILY STOIC // FILOSOFIA VISSUTA"
                                                                  : "OBLIQUE STRATEGIES // BRIAN ENO";
  drawPill(t, 34, 62, 24, deckTag, ui::kFontMicro, true, 12);

  if (justShuffled_) {
    drawPill(t, static_cast<int16_t>(w - 150), 62, 24, "SHUFFLE (IMU)!", ui::kFontMicro, false, 8);
  }

  // Giant decorative quotes marks
  drawText(t, Rect{40, 102, 60, 48}, "\"", ui::kFontHero, Color::Black);

  // Main Quote in Noto Serif style (FreeInkUI layout with multiline)
  TextStyle quoteStyle;
  quoteStyle.font = static_cast<freeink::ui::FontId>(ui::kFontTitle);
  quoteStyle.color = Color::Black;
  quoteStyle.align = TextAlign::Left;
  quoteStyle.maxLines = 6;
  t.text(Rect{44, 150, static_cast<int16_t>(w - 88), 220}, card.quote, quoteStyle);

  // Decorative dashed divider line
  const int16_t divY = 410;
  for (int16_t x = 44; x < w - 44; x += 8) {
    fastFill(t, Rect{x, divY, 4, 1}, Paint::solid(Color::Black), 0);
  }

  // Author & Source
  drawText(t, Rect{44, 430, static_cast<int16_t>(w - 88), 28}, card.author, ui::kFontBody, Color::Black);
  drawText(t, Rect{44, 460, static_cast<int16_t>(w - 88), 22}, card.source, ui::kFontSmall, Color::Black);

  // Practical Takeaway / Challenge Box at bottom of card
  const Rect actionBox{34, 510, static_cast<int16_t>(w - 68), 196};
  fastFill(t, actionBox, Paint::solid(Color::Black), 8);

  // Inner white card for takeaway
  fastStroke(t, actionBox, Paint::solid(Color::White), 2, 8);
  drawPill(t, 46, 524, 22, "SPUNTO PRATICO PER LA GIORNATA", ui::kFontMicro, false, 10);

  TextStyle actStyle;
  actStyle.font = static_cast<freeink::ui::FontId>(ui::kFontBody);
  actStyle.color = Color::White;
  actStyle.align = TextAlign::Left;
  actStyle.maxLines = 4;
  t.text(Rect{46, 560, static_cast<int16_t>(w - 92), 120}, card.takeaway, actStyle);

  // Shake IMU notice
  drawText(t, Rect{46, 676, static_cast<int16_t>(w - 92), 18},
           "Suggerimento: scuoti fisicamente l'X3 per pescare!", ui::kFontMicro, Color::White);

  // Bottom Key Bar
  drawBottomKeyBar(t, "DESTRA", "Altro Mazzo", "OK", "Nuova Carta", "BACK", "Esci");
}

}  // namespace inkling
