# Mission validation audit

Catalog checked on 2026-10-06. No game was launched during this audit.

CSV SHA-256: `fe0ec02e1572d061a02aef48431dc2a6c543a8e6f43cb60c8aeb49ebd320538c`

All 55 enabled definitions are accepted by the actual runtime CSV parser. Names are unique. Object inventories were checked against saved native RAM for each course and mode. Inventory checks exclude terrain-height filtering and do not prove that every target is physically reachable. Automated judging/adapter tests verify shared logic, not completion of each route.

Fixed a catalog-loading failure: `direction=opposite` now aliases `reverse`. A regression test covers the alias.

A definition/discovery pass is not a native play pass. The table preserves that distinction for every mission.

| Mission | Course | Type | Definition | Target inventory / remaining verification |
|---|---|---|---|---|
| FIRST TURN | town | DRIVE | Pass | Shared judge covered; individual native completion not established by this audit. |
| DON'T TOUCH THE SIDES | town | DRIVE | Pass | Shared judge covered; individual native completion not established by this audit. |
| POINT TO POINT | town | DRIVE | Pass | Shared judge covered; individual native completion not established by this audit. |
| BEHIND SCHEDULE | town | DRIVE | Pass | Shared judge covered; individual native completion not established by this audit. |
| CLOISTER FORK | town | DRIVE | Pass | Shared judge covered; individual native completion not established by this audit. |
| ARCHERY | town | DESTROY | Pass | Shared judge covered; individual native completion not established by this audit. |
| CHICANE | town | DRIVE | Pass | Shared judge covered; individual native completion not established by this audit. |
| KIOSK MODE | town | COLLECT | Pass | 6 candidates / 6 required; discovery passed |
| CONE BUT NOT FORGOTTEN | town | DESTROY | Pass | 35 candidates / 35 required; discovery passed |
| PEOPLE PLEASER | town | DODGE | Pass | 30 candidates / 20 required; discovery passed |
| SUPERMARKET SWEEP | town | DESTROY | Pass | Side contact remains unverified; native front contact passed. |
| TAKE A SEAT | town | DESTROY | Pass | 55 candidates / 55 required; discovery passed |
| AN APPLE A DAY | town | DESTROY | Pass | 30 candidates / 3 required; discovery passed |
| PHONE HOME | town | DESTROY | Pass | 6 candidates / 6 required; discovery passed |
| GARBAGE COLLECTOR | town | DESTROY | Pass | 7 candidates / 7 required; discovery passed |
| MANY RIVERS TO CROSS | town | JUMP | Pass | Shared judge covered; individual native completion not established by this audit. |
| SPEED DEMON | town | SPEED | Pass | Shared judge covered; individual native completion not established by this audit. |
| FLOWER POWER | town | DESTROY | Pass | 34 candidates / 34 required; discovery passed |
| A BRIDGE TOO FAR | mountain | JUMP | Pass | Corrected five-gate route loads; corrected route has not been driven to completion. |
| MOUNTAIN GOAT | mountain | TURN | Pass | 180-degree clean turn not proven in native play. |
| NOT MUCH OF A GARDEN | mountain | TURN | Pass | 180-degree clean turn not proven in native play. |
| THE OLDE TOWN | mountain | DRIVE | Pass | Shared judge covered; individual native completion not established by this audit. |
| ONTO THE BRIDGE | mountain | JUMP | Pass | Shared judge covered; individual native completion not established by this audit. |
| PARKOUR TICKET | mountain | STUNT | Pass | Physical wall ride needs native completion verification. |
| PADDLING OUT | mountain | DRIVE | Pass | Standing start and end plane inspected; scripted runs missed finish width. |
| MAKE YOUR MIND UP | mountain | DRIVE | Pass | Shared judge covered; individual native completion not established by this audit. |
| STICKS AND STONES | coast | DESTROY | Pass | 59 candidates / 59 required; discovery passed |
| SIGN OF THE TIMES | coast | DESTROY | Pass | 36 candidates / 36 required; discovery passed |
| PARASOL STARS | coast | DESTROY | Pass | 17 candidates / 17 required; discovery passed |
| AN APPLE A DAY II | coast | DESTROY | Pass | 20 candidates / 5 required; discovery passed |
| FLOWER POWER II | coast | DESTROY | Pass | 70 candidates / 70 required; discovery passed |
| GARBAGE COLLECTOR II | coast | DESTROY | Pass | 15 candidates / 15 required; discovery passed |
| TAKE A SEAT II | coast | DESTROY | Pass | 48 candidates / 48 required; discovery passed |
| PHONE HOME II | coast | DESTROY | Pass | 6 candidates / 6 required; discovery passed |
| TABLE MANNERS II | coast | DESTROY | Pass | 10 candidates / 10 required; discovery passed |
| VIEWPOINT | coast | DESTROY | Pass | 6 candidates / 6 required; discovery passed |
| TAKE THE HIGH ROAD | coast | JUMP | Pass | Shared judge covered; individual native completion not established by this audit. |
| LOVE BUG | coast | COLLECT | Pass | 2 candidates / 2 required; discovery passed |
| CROSSROADS | coast | DRIVE | Pass | Shared judge covered; individual native completion not established by this audit. |
| MAN WITH A VAN | coast | COLLECT | Pass | 3 candidates / 3 required; discovery passed |
| WINDOW SHOPPING | coast | DRIVE | Pass | Shared judge covered; individual native completion not established by this audit. |
| JUMP THE SHARK | coast | JUMP | Pass | Shared judge covered; individual native completion not established by this audit. |
| ODD ONE OUT | coast | COLLECT | Pass | 1 candidates / 1 required; discovery passed |
| TWO WHEELS GOOD | coast | STUNT | Pass | Shared judge covered; individual native completion not established by this audit. |
| PULL THE LEVER | coast | TURN | Pass | 180-degree clean turn not proven in native play. |
| STAIRWAY TO HEAVEN | coast | JUMP | Pass | Shared judge covered; individual native completion not established by this audit. |
| UNBREAKABLE | town | DRIVE | Pass | Shared judge covered; individual native completion not established by this audit. |
| UNBREAKABLE II | coast | DRIVE | Pass | Shared judge covered; individual native completion not established by this audit. |
| UNBREAKABLE III | mountain | DRIVE | Pass | Shared judge covered; individual native completion not established by this audit. |
| THRILL DRIVE | town | DESTROY | Pass | 50 candidates / 10 required; discovery passed |
| THRILL DRIVE II | coast | DESTROY | Pass | 38 candidates / 10 required; discovery passed |
| THRILL DRIVE III | mountain | DESTROY | Pass | 21 candidates / 10 required; discovery passed |
| DO A BARREL ROLL | mountain | STUNT | Pass | Both roll directions need native completion verification. |
| CAT PERSON | mountain | COLLECT | Pass | 3 candidates / 1 required; discovery passed |
| AVENUE OF CHAMPIONS | town | DRIVE | Pass | Shared judge covered; individual native completion not established by this audit. |

## Scope limits

This finishes the catalog and automated validation pass. It does not certify all 55 missions as play-tested. Remaining native gameplay checks include stall sides, handbrake turns, the corrected broken-bridge route, Paddling Out, wall riding and barrel rolls. Reverse/mirror behavior on every course also needs a full native play pass. No definitions were removed or objectives weakened to make the audit pass.
