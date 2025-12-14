use strict;
use warnings;
use tests::tests;
check_expected ([<<'EOF']);
(bestfit) begin
(bestfit) Allocated A (10 pages) at index 0
(bestfit) Allocated B (2 pages) at index 10
(bestfit) Allocated C (5 pages) at index 12
(bestfit) Freed A (10 pages)
(bestfit) Allocated D (3 pages) at index 0 - Best Fit test
(bestfit) end
EOF
pass;
