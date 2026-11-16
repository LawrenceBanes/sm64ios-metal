    // Two raised meadow bands, each a three-cell grid (twelve triangles).
    COL_TRI(506, 512, 510),
    COL_TRI(512, 513, 510),
    COL_TRI(510, 513, 511),
    COL_TRI(513, 514, 511),
    COL_TRI(511, 514, 507),
    COL_TRI(514, 515, 507),
    COL_TRI(512, 516, 513),
    COL_TRI(516, 517, 513),
    COL_TRI(513, 517, 514),
    COL_TRI(517, 518, 514),
    COL_TRI(514, 518, 515),
    COL_TRI(518, 519, 515),

    // The northbound transition is one continuous, gently rising strip.
    COL_TRI(517, 520, 518),
    COL_TRI(520, 521, 518),
    COL_TRI(520, 522, 521),
    COL_TRI(522, 523, 521),

    // Low outer walls keep ordinary traversal on the authored meadow while
    // leaving the north connector open for a future attached region.
    COL_TRI(506, 524, 512),
    COL_TRI(506, 508, 524),
    COL_TRI(512, 526, 516),
    COL_TRI(512, 524, 526),
    COL_TRI(507, 515, 525),
    COL_TRI(507, 525, 509),
    COL_TRI(515, 519, 527),
    COL_TRI(515, 527, 525),
