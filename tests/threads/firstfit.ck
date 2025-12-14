# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected ([<<'EOF']);
(firstfit) begin
(firstfit) Allocated A at index 0
(firstfit) Allocated B at index 4
(firstfit) Freed A
(firstfit) Allocated C at index 0
(firstfit) end
EOF
pass;
