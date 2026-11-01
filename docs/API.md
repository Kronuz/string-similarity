# string-similarity API reference

## Usage

```cpp
#include <string>

#include "jaro_winkler.h"
#include "jaccard.h"
#include "levenshtein.h"
#include "sorensen_dice.h"
#include "phonetic_metric.h"
#include "english_soundex.h"

Jaro jaro;
jaro.similarity(std::string("MARTHA"), std::string("MARHTA"));   // ~0.944

Jaccard jac;
jac.similarity(std::string("night"), std::string("nacht"));      // 3/7, over char sets

Sorensen_Dice sd;
sd.similarity(std::string("night"), std::string("nacht"));       // 0.25, over char bigrams

// Phonetic: encode with English Soundex, score the codes with Levenshtein.
PhoneticMetric<SoundexEnglish, Levenshtein> phon("Robert");
phon.similarity(std::string("Rupert"));                           // homophones share a code
```

The operands are `std::string` or `std::string_view` (the metrics call `.empty()` on
them), both of the same type; a string literal does not bind. A metric bound to a
string in its constructor copies and moves like any value: the copy owns its string.

Every metric defaults to `icase=true`, which means "fold both operands to upper case,
then compare": the result is the one `icase=false` gives for the folded strings, for the
two-argument and the bound forms alike. Pass `false` to compare case-sensitively.
`distance()` returns `0` for identical strings and `1` when either side is empty;
`similarity()` is the mirror, `1` and `0`. Two one-character strings that are equal
after folding are identical, so `Sorensen_Dice` scores `a` and `A` as `1`.
