# string-similarity API reference

## Usage

```cpp
#include "jaro_winkler.h"
#include "jaccard.h"
#include "sorensen_dice.h"
#include "phonetic_metric.h"
#include "english_soundex.h"

Jaro jaro;
jaro.similarity("MARTHA", "MARHTA");               // ~0.944

Jaccard jac;
jac.similarity("night", "nacht");                  // 3/7, over char sets

Sorensen_Dice sd;
sd.similarity("night", "nacht");                   // 0.25, over char bigrams

// Phonetic: encode with English Soundex, score the codes with Levenshtein.
PhoneticMetric<SoundexEnglish, Levenshtein> phon("Robert");
phon.similarity("Rupert");                          // homophones share a code
```

Every metric defaults to `icase=true`, folding both sides to upper case before
comparing. Pass `false` to compare case-sensitively. `distance()` returns `0` for
identical strings and `1` when either side is empty; `similarity()` is the
mirror, `1` and `0`.
