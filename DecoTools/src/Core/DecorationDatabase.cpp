// Pewpew's Deco Tools - Local Decoration Database
// Provides the built-in decoration catalog, loads or creates decorations.db.json,
// and resolves decoration names and IDs for XML tools and API count operations.

#include "DecorationDatabase.h"
#include "Gw2Api.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <future>
#include <iterator>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace
{
    const DecorationDatabase::Entry Entries[] =
    {
        { "A Day in Kryta", 625, 817 },
        { "Academic Light", 469, 1262 },
        { "Academic Wall", 15, 1253 },
        { "Academic Wall with Windows", 609, 1254 },
        { "Aetherblade Hideout Banner", 1148, 1499 },
        { "Antique Bell", 830, 1296 },
        { "Antler Pattern Snowflake Platform", 476, 947 },
        { "Armor Display Case", 437, -1 },
        { "Armor Display Case—Mannequin", 265, -1 },
        { "Armor Stand", 350, 386 },
        { "Arrow Cart Siege", 55, 849 },
        { "Arrow Pattern Snowflake Platform", 261, 951 },
        { "Ascalonian Lamp", 207, 395 },
        { "Ascalonian Pillar", 709, 577 },
        { "Ascalonian Tree", 39, 338 },
        { "Astral Ward Door", 477, 1247 },
        { "Astral Ward Gate", 293, 1256 },
        { "Astral Ward Orb Stand", 466, 1259 },
        { "Astral Ward Pillar", 546, 1249 },
        { "Astral Ward Square Floor", 196, 1257 },
        { "Astral Ward Staircase", 208, 1251 },
        { "Astral Ward Tiled Circular Platform", 289, 1260 },
        { "Astral Ward Wall", 334, 1261 },
        { "Asuran Summit Banner", 538, 393 },
        { "Asuran Summit Flag", 563, 542 },
        { "Awakened Bone Column", 649, 892 },
        { "Awakened Bone Wall", 520, 894 },
        { "Awakened Tar Drip", 109, 868 },
        { "Awakened Tar Pit", 428, 905 },
        { "Ballista Siege", 698, 852 },
        { "Balthazar Statue", 324, 362 },
        { "Bamboo Awning", 965, 1350 },
        { "Bamboo Canopy", 966, 1395 },
        { "Bamboo Wall (240 x 240)", 975, 1360 },
        { "Basic Basket", 239, 601 },
        { "Basic Bookshelf", 491, 224 },
        { "Basic Boulder", 781, 589 },
        { "Basic Candle", 245, 437 },
        { "Basic Chair", 810, 413 },
        { "Basic Column", 307, 574 },
        { "Basic Commemorative Statue", 171, 763 },
        { "Basic Crate", 680, 516 },
        { "Basic Flagpole", 251, 352 },
        { "Basic Fountain", 449, 376 },
        { "Basic Grave Marker", 455, 764 },
        { "Basic Pedestal", 99, 548 },
        { "Basic Planter", 529, 223 },
        { "Basic Shrub", 376, 209 },
        { "Basic Signpost", 186, 762 },
        { "Basic Table", 325, 202 },
        { "Basic Torch", 812, 582 },
        { "Basic Tree", 572, 620 },
        { "Bat Lanterns", 597, 1164 },
        { "Beach House Spiral Stairs", 943, 1292 },
        { "Beetletun Statue Fragments", 287, 830 },
        { "Black Lion Drum Set", 1188, -1 },
        { "Block of the Solid Ocean", 452, 786 },
        { "Block Topiary", 341, 270 },
        { "Bloodstone Turret Fragment", 799, 757 },
        { "Blue Balloon", 25, 503 },
        { "Blue Cushion", 666, 179 },
        { "Blue Dragon Target", 426, 1057 },
        { "Blue Pirate Flag", 61, 226 },
        { "Blue Wintersday Gift", 374, 952 },
        { "Boar Statue", 98, 1027 },
        { "Boardwalk (240 x 1920)", 978, 1440 },
        { "Boardwalk (240 x 960)", 1025, 1408 },
        { "Boardwalk (480 x 960)", 964, 1411 },
        { "Body Topiary", 56, 521 },
        { "Bonfire", 197, 126 },
        { "Bowl Topiary", 489, 198 },
        { "Branchless Bush", 1013, 1351 },
        { "Branded Crystal", 447, 870 },
        { "Branded Devourer Monument", 283, 1065 },
        { "Branded Spire", 696, 872 },
        { "Brasswork Big Gear", 931, -1 },
        { "Brasswork Curved Pipe", 920, -1 },
        { "Brasswork Engine", 916, -1 },
        { "Brasswork Pipe Cap", 917, -1 },
        { "Brasswork Pipe Connector", 932, -1 },
        { "Brasswork Small Gear", 925, -1 },
        { "Brasswork Straight Pipe", 918, -1 },
        { "Brasswork T-Pipe", 912, -1 },
        { "Brasswork Wall", 924, -1 },
        { "Broken Ascalonian Pillar", 504, 153 },
        { "Broken Mast Pole", 74, 263 },
        { "Broken Sandstone Pillar", 377, 247 },
        { "Broken Square Pillar", 264, 612 },
        { "Bronze Cairn the Indomitable Trophy", 739, 818 },
        { "Bronze Cardinal Adina Trophy", 320, 1051 },
        { "Bronze Cardinal Sabir Trophy", 454, 1050 },
        { "Bronze Chak Gerent Trophy", 577, 688 },
        { "Bronze Conjured Amalgamate Trophy", 200, 979 },
        { "Bronze Decima Trophy", 841, 1304 },
        { "Bronze Deimos Trophy", 482, 829 },
        { "Bronze Desmina Trophy", 757, 942 },
        { "Bronze Dhuum Trophy", 365, 941 },
        { "Bronze Ether Djinn Trophy", 391, 1046 },
        { "Bronze Gorseval Trophy", 68, 700 },
        { "Bronze Greer Trophy", 847, 1300 },
        { "Bronze Keep Construct Trophy", 547, 750 },
        { "Bronze Mordremoth Trophy", 333, 708 },
        { "Bronze Mursaat Overseer Trophy", 758, 820 },
        { "Bronze Qadim Trophy", 503, 976 },
        { "Bronze River of Souls Trophy", 30, 922 },
        { "Bronze Sabetha Trophy", 575, 679 },
        { "Bronze Samarog Trophy", 393, 826 },
        { "Bronze Shatterer Trophy", 719, 696 },
        { "Bronze Siege the Stronghold Trophy", 745, 760 },
        { "Bronze Slothasor Trophy", 790, 669 },
        { "Bronze Statue of Grenth Trophy", 127, 914 },
        { "Bronze Tequatl Trophy", 240, 706 },
        { "Bronze Triple Trouble Trophy", 112, 686 },
        { "Bronze Twin Largos Trophy", 695, 985 },
        { "Bronze Ura Trophy", 850, 1302 },
        { "Bronze Vale Guardian Trophy", 492, 672 },
        { "Bronze White Mantle Abomination Trophy", 88, 707 },
        { "Bronze Xera Trophy", 91, 749 },
        { "Bundle of Corn Stalks", 487, 634 },
        { "Burlap Sack", 967, 1378 },
        { "Burlap Sack Pile", 998, 1415 },
        { "Cabana Roof Long (A)", 1005, 1441 },
        { "Cabana Roof Long (B)", 962, 1386 },
        { "Cabana Wall (120)", 1014, 1393 },
        { "Cabana Wall (960)", 1008, 1394 },
        { "Café Chair", 884, -1 },
        { "Café Console", 887, -1 },
        { "Café Drink Station", 882, -1 },
        { "Café Food Station", 885, -1 },
        { "Café Guide Beacon", 888, -1 },
        { "Café Souvenir Station", 886, -1 },
        { "Café Transporter", 883, -1 },
        { "Cairn the Indomitable Shard", 623, 812 },
        { "Campfire", 750, 148 },
        { "Candy Cane Beam", 553, 1136 },
        { "Candy Cane Ramp", 199, 1137 },
        { "Cannon Siege", 728, 863 },
        { "Canthan Condiments", 309, -1 },
        { "Canthan Garden Wall", 110, 1244 },
        { "Canthan Glass Bottle", 1043, 1388 },
        { "Canthan Glass Bottle (Full)", 996, 1384 },
        { "Canthan Lantern", 1035, 1435 },
        { "Canthan Nian Statue", 536, -1 },
        { "Canthan Stair Bridge", 442, 1243 },
        { "Capped Gold Pillar", 354, 278 },
        { "Cardinal Adina's Token", 386, 1049 },
        { "Cardinal Sabir's Token", 413, 1048 },
        { "Catapult Siege", 321, 866 },
        { "Ceramic Planter", 269, 471 },
        { "Chak Gerent Eye", 192, 676 },
        { "Chandelier", 194, 819 },
        { "Changing Tent", 1034, 1423 },
        { "Charr Effigy", 854, -1 },
        { "Charr Heliplatform", 151, 1067 },
        { "Charr Scrap Cannon", 277, 1063 },
        { "Charr Statue", 406, 249 },
        { "Charr Summit Banner", 51, 449 },
        { "Charr Summit Flag", 331, 430 },
        { "Charr Tank", 323, 1066 },
        { "Cheery Balloon Bundle", 639, 144 },
        { "Chunk of the Solid Ocean", 798, 776 },
        { "Clock Tower Gear", 254, 1166 },
        { "Clock Tower's Broken Beam", 176, 1266 },
        { "Column (30 x 480 x 30)", 981, 1383 },
        { "Commemorative Weapon Stand", 453, 658 },
        { "Complete Academic Arch", 339, 1252 },
        { "Comprehensive Disciplines Research Commemorative Statue", 158, 624 },
        { "Conjured Amalgamate's Token", 541, 986 },
        { "Corrupted Eagle Shrine", 784, 1143 },
        { "Corrupted Ox Shrine", 344, 1142 },
        { "Corrupted Wolverine Shrine", 691, 1144 },
        { "Creepy Jack-o'-Lantern", 786, 132 },
        { "Crooked Mushroom", 517, 121 },
        { "Crooked Thorny Mushroom", 612, 411 },
        { "Crystal Block of the Solid Ocean", 519, 788 },
        { "Crystal Prism", 459, 1181 },
        { "Cube of Snow", 284, 798 },
        { "Cuboid of Snow", 683, 796 },
        { "Curved Roof (600)", 971, 1382 },
        { "Curved Roof (960)", 983, 1404 },
        { "Curved Roof Corner", 1011, 1352 },
        { "Curved Roof Dormer", 1002, 1406 },
        { "Decima Shard", 1131, 1475 },
        { "Decima's Token (Guild Decoration)", -1, 1303 },
        { "Decima's Token (Homestead)", 855, -1 },
        { "Decorated Casket", 336, 995 },
        { "Decorative Outdoor Rug", 879, -1 },
        { "Deep Pot", 366, -1 },
        { "Deimos Statue", 1123, 1502 },
        { "Deluxe Charr Copter", 602, 332 },
        { "Deluxe Vine Wall", -1, 517 },
        { "Deluxe Wyvern Trophy", 75, 219 },
        { "Demolished Mast Pole", 687, 205 },
        { "Demon Statue", 485, 792 },
        { "Desert Sofa", 1134, 1452 },
        { "Desmina's Token", 233, 919 },
        { "Dhuum's Token", 26, 917 },
        { "Dhuum's Torch", 1132, 1451 },
        { "Distressed Lion Statue", 388, 164 },
        { "Divinity Lamp", 64, 626 },
        { "Divinity Streetlamp", 166, 540 },
        { "Djinn Launching Device", 148, 1044 },
        { "Dock Crane", 255, 1222 },
        { "Dog Statue", 657, 956 },
        { "Doorway Wall", 1055, 1343 },
        { "Dragon Bash Banner", 717, 1161 },
        { "Dragon Bash Firework Launcher", 658, 1186 },
        { "Dragon Bash Kite", 661, 1183 },
        { "Dragon Bash Pylon", 95, 1184 },
        { "Dragon Bash Windcatcher", 561, 1187 },
        { "Dragon Hologram Generator", 126, 1056 },
        { "Dragon Plate", 60, 1154 },
        { "Dragon Statue", 870, 1280 },
        { "Dragon's Breath Lantern", 744, 1054 },
        { "Dragon's Tooth Lantern", 351, 1055 },
        { "Draped Wintersday Garland", 319, 1170 },
        { "Dwayna Statue", 163, 533 },
        { "Earth Elemental Left Hand", 1102, 1513 },
        { "Earth Elemental Right Hand", 1157, 1507 },
        { "Effervescent Pod", 162, 1036 },
        { "Elaborate Sandstone Pillar", 141, 642 },
        { "Elegant Pillar", 332, 507 },
        { "Elegant Square Pillar", 132, 497 },
        { "Elegant Wall Panel", 588, 499 },
        { "Elonian Bazaar Shade", 945, 1290 },
        { "Elonian Cairn Stones", 635, 898 },
        { "Elonian Gong", 615, 875 },
        { "Elonian Hawk Statue", 340, 882 },
        { "Elonian Incense Stand", 674, 899 },
        { "Elonian Lattice", 647, 903 },
        { "Elonian Railing", 806, 886 },
        { "Elonian Snake Statue", 767, 889 },
        { "Elonian Stone Tower", 501, 879 },
        { "Elonian Teapot", 164, 869 },
        { "Elonian Tent", 288, 887 },
        { "Elonian Urn", 488, 877 },
        { "Elonian Vase", 513, 906 },
        { "Elonian Wood Chair", 190, 891 },
        { "Elonian Wood Table", 498, 873 },
        { "Embellished Wintersday Star", 398, 1172 },
        { "Empty Square Planter", 150, 212 },
        { "Entryway Wall (Double)", 979, 1401 },
        { "Entryway Wall (Single)", 1018, 1354 },
        { "Ephemeral Spider's Web Floor", 645, 1224 },
        { "Ephemeral Spider's Web Wall", 414, 1226 },
        { "Eternal Flame", 581, 791 },
        { "Ether Djinn's Token", 116, 1052 },
        { "Fancy Armchair", 537, 643 },
        { "Fancy Chair", 52, 599 },
        { "Fancy Desert Sofa", 1130, 1493 },
        { "Fancy Round Table", 343, 293 },
        { "Fancy Small Rug", 876, -1 },
        { "Fancy Square Rug", 878, -1 },
        { "Fancy Table", 302, 175 },
        { "Fancy Urn", 474, 809 },
        { "Fancy Wagon", 684, 464 },
        { "Fanned Island Fern Patch", 969, 1344 },
        { "Female Norn Holo-Dancer", 571, 767 },
        { "Festival Tent", 371, 177 },
        { "Festive Arch", 41, 316 },
        { "Festive Balloon Bundle", 751, 277 },
        { "Festive Fall Tent", 955, 1336 },
        { "Festive Streetlamp", 397, 664 },
        { "Fine Armor Stand", 236, 257 },
        { "Fire Circle", 736, 401 },
        { "Firecracker", 10, 695 },
        { "Flame Ram Siege", 531, 858 },
        { "Flame-Bearing Gargoyle", 825, 1294 },
        { "Floor (960 x 600)", 988, 1355 },
        { "Floor (960 x 840)", 1045, 1443 },
        { "Floral Canthan Rug", 215, -1 },
        { "Fog Machine", 467, 949 },
        { "Forged Brazier", 475, 878 },
        { "Forged Fire Wall", 704, 900 },
        { "Forged Pylon", 747, 902 },
        { "Fractal Console", 304, 924 },
        { "Fragment of Saul's Burden", 502, 806 },
        { "Fragments of the Solid Ocean", 102, 769 },
        { "Freezie's Heart Statue", 232, 1022 },
        { "Frost Legion Machine", 193, 1163 },
        { "Fuchsia Balloon", 328, 372 },
        { "Full Wall Bracket (480)", 1037, 1363 },
        { "Full Wall Bracket (960)", 987, 1436 },
        { "Full Wall Bracket Corner", 972, 1346 },
        { "Fun Balloon Bundle", 113, 333 },
        { "Galleon Door", 1052, 1405 },
        { "Galleon Hull (Middle)", 989, 1427 },
        { "Galleon Hull (Stern)", 1000, 1414 },
        { "Galleon Hull Entryway", 1054, 1397 },
        { "Galleon Hull with Gunports", 1040, 1364 },
        { "Gargoyle", 838, 1297 },
        { "Ghostly Dining Chair", 532, 911 },
        { "Ghostly Dining Table", 187, 908 },
        { "Giant Candy Cane", 738, 1140 },
        { "Gift Barrel", 1083, 1450 },
        { "Gift Levitator", 1077, 1448 },
        { "Gilded Banner", 457, 280 },
        { "Gilded Lamp", 219, 192 },
        { "Gingerbread Candy Bush", 1080, -1 },
        { "Gingerbread Door", 1079, -1 },
        { "Gingerbread Doorway", 1088, -1 },
        { "Gingerbread Friend", 1085, -1 },
        { "Gingerbread Frosting Trim", 1087, -1 },
        { "Gingerbread Gable", 1082, -1 },
        { "Gingerbread Roof", 1081, -1 },
        { "Gingerbread Wall", 1084, -1 },
        { "Gingerbread Window", 1089, -1 },
        { "Gingerbread-Man Ice Sculpture", 681, 948 },
        { "Glacier Overhang", 160, 1232 },
        { "Globe of Whispers", 724, 732 },
        { "Gold Cairn the Indomitable Trophy", 772, 828 },
        { "Gold Cardinal Adina Trophy", 444, 1042 },
        { "Gold Cardinal Sabir Trophy", 785, 1041 },
        { "Gold Chak Gerent Trophy", 465, 673 },
        { "Gold Conjured Amalgamate Trophy", 294, 991 },
        { "Gold Decima Trophy", 843, 1307 },
        { "Gold Deimos Trophy", 314, 824 },
        { "Gold Desmina Trophy", 525, 939 },
        { "Gold Dhuum Trophy", 642, 915 },
        { "Gold Ether Djinn Trophy", 380, 1043 },
        { "Gold Firepit", 735, 596 },
        { "Gold Gorseval Trophy", 360, 710 },
        { "Gold Greer Trophy", 853, 1299 },
        { "Gold Keep Construct Trophy", 401, 758 },
        { "Gold Mordremoth Trophy", 217, 702 },
        { "Gold Mursaat Overseer Trophy", 106, 822 },
        { "Gold Pillar", 545, 230 },
        { "Gold Qadim Trophy", 195, 967 },
        { "Gold River of Souls Trophy", 514, 926 },
        { "Gold Sabetha Trophy", 358, 691 },
        { "Gold Samarog Trophy", 788, 832 },
        { "Gold Shatterer Trophy", 212, 667 },
        { "Gold Siege the Stronghold Trophy", 370, 751 },
        { "Gold Slothasor Trophy", 579, 693 },
        { "Gold Statue of Grenth Trophy", 313, 938 },
        { "Gold Tequatl Trophy", 629, 713 },
        { "Gold Triple Trouble Trophy", 742, 682 },
        { "Gold Twin Largos Trophy", 803, 978 },
        { "Gold Ura Trophy", 848, 1298 },
        { "Gold Vale Guardian Trophy", 342, 678 },
        { "Gold Wall", 363, 250 },
        { "Gold White Mantle Abomination Trophy", 652, 701 },
        { "Gold Xera Trophy", 318, 754 },
        { "Golden Dragon Statue", 868, 1279 },
        { "Golden Horse Statue", 1166, 1468 },
        { "Golden Rabbit Statue", 16, 1234 },
        { "Golden Sink", 542, -1 },
        { "Golden Snake Statue", 866, 1314 },
        { "Golem Siege", 238, 856 },
        { "Gorseval Tentacle", 675, 668 },
        { "Gramophone", 725, 339 },
        { "Grand Clock Tower Gear", 787, 1165 },
        { "Grandfather Clock", 809, 261 },
        { "Graveyard Charrgoyle", 957, -1 },
        { "Graveyard Column", 958, -1 },
        { "Graveyard Curved Stone Path", 956, -1 },
        { "Graveyard Fence", 952, -1 },
        { "Graveyard Fog", 950, -1 },
        { "Graveyard Gate", 949, -1 },
        { "Graveyard Straight Stone Path", 947, -1 },
        { "Graveyard Tombstone", 954, -1 },
        { "Green Balloon", 445, 432 },
        { "Green Cushion", 280, 185 },
        { "Green Pirate Flag", 446, 567 },
        { "Green Tree", 805, 371 },
        { "Green Wintersday Gift", 716, 946 },
        { "Greer's Token (Guild Decoration)", -1, 1301 },
        { "Greer's Token (Homestead)", 849, -1 },
        { "Grenth Statue", 145, 344 },
        { "Grenth's Palm", 497, 1034 },
        { "Griffon Fountain", 511, 374 },
        { "Griffon Rental Post", 177, 1246 },
        { "Griffon Statue", 540, 317 },
        { "Guild Arrow Cart Siege", 423, 867 },
        { "Guild Ballista Siege", 211, 854 },
        { "Guild Banquet Table", 710, 616 },
        { "Guild Bar", 526, 394 },
        { "Guild Barstool", 440, 273 },
        { "Guild Bench", 326, 438 },
        { "Guild Catapult Siege", 136, 857 },
        { "Guild Chair", 167, 294 },
        { "Guild Initiative Banner", 664, 173 },
        { "Guild Shield Generator Siege", 168, 850 },
        { "Guild Stool", 427, 319 },
        { "Guild Trebuchet Siege", 424, 860 },
        { "Hanging Brazier", 1064, 1398 },
        { "Hanging Tree", 272, 794 },
        { "Hardy Island Fern", 1026, 1389 },
        { "Haunted Armchair", 345, 912 },
        { "Haunted Bell", 840, 1295 },
        { "Haunted Love Seat", 770, 913 },
        { "Head Topiary", 558, 155 },
        { "Hedge", 368, 150 },
        { "Hedge Corner", 560, 573 },
        { "Hedge Pillar", 220, 305 },
        { "Hedge Planter", 633, 370 },
        { "Hemisphere of Snow", 774, 802 },
        { "Highback Chair", 630, 448 },
        { "Holiday Wreath", 730, 663 },
        { "Holographic Track Curved Ramp", 229, 1239 },
        { "Holographic Track Downward Left Curve", 270, 1212 },
        { "Holographic Track Downward Ramp", 462, 1219 },
        { "Holographic Track Downward Right Curve", 105, 1209 },
        { "Holographic Track Large Curved Ramp", 524, 1240 },
        { "Holographic Track Left Curve", 533, 1208 },
        { "Holographic Track Long Straightaway", 776, 1214 },
        { "Holographic Track Right Curve", 159, 1215 },
        { "Holographic Track Straightaway", 78, 1217 },
        { "Holographic Track Upward Left Curve", 509, 1211 },
        { "Holographic Track Upward Ramp", 17, 1218 },
        { "Holographic Track Upward Right Curve", 178, 1213 },
        { "Holographic Track Wide Curved Ramp", 310, 1241 },
        { "Horse Statue", 1133, 1487 },
        { "Human Summit Banner", 779, 214 },
        { "Human Summit Flag", 551, 343 },
        { "Ice Castle: Floor", 3, 1024 },
        { "Ice Castle: Roof", 583, 1026 },
        { "Ice Castle: Turret", 773, 1023 },
        { "Ice Castle: Wall", 155, 1025 },
        { "Ice Sheet", 654, 1174 },
        { "Ice Slide", 702, 1230 },
        { "Icicle", 857, 1311 },
        { "Illuminated Fountain", 769, 544 },
        { "Immense Lion Statue", 32, 595 },
        { "Impaled Prisoner", 128, 805 },
        { "Incredulous Stage", 783, 1156 },
        { "Infernal Facade", 814, 1039 },
        { "Infinirarium Deck", 50, 1198 },
        { "Iron Column 16 x 240 x 16", 1116, 1467 },
        { "Iron Column 30 x 240 x 30", 1115, 1511 },
        { "Iron Door", 1215, 1542 },
        { "Iron Doorway 240 x 240", 1199, 1537 },
        { "Iron Floor 960 x 600", 1204, 1536 },
        { "Iron Floor 960 x 840", 1217, 1544 },
        { "Iron Stairs 120 x 120", 1206, 1552 },
        { "Iron Stairs 120 x 60", 1209, 1546 },
        { "Iron Wall 120 x 240", 1214, 1534 },
        { "Iron Wall 240 x 240", 1194, 1551 },
        { "Iron Wall 60 x 240", 1203, 1538 },
        { "Iron Wall 600 x 240", 1211, 1554 },
        { "Iron Wall 960 x 240", 1208, 1556 },
        { "Iron Window", 1202, 1543 },
        { "Island Fern", 995, 1367 },
        { "Island Fern (Large)", 963, 1396 },
        { "Island Fern (Varied)", 1058, 1377 },
        { "Island Pitcher Plant Patch", 1036, 1442 },
        { "Island Shrub", 1039, 1430 },
        { "Island Shrub (Bunch)", 968, 1374 },
        { "Island Tree (Curved)", 1016, 1392 },
        { "Island Tree (Short)", 1022, 1368 },
        { "Island Tree (Tall)", 1024, 1446 },
        { "Jack-o'-Lantern", 201, 279 },
        { "Jackal Rental Post", 896, 1284 },
        { "Jade Bot Workbench", 594, 1200 },
        { "Jade Tech Microwave", 852, -1 },
        { "Janthir Grass (Large)", 402, -1 },
        { "Janthir Grass (Small)", 292, -1 },
        { "Jewelry Display Case", 620, -1 },
        { "Jolly Bow", 858, 1312 },
        { "Jormag Hologram Generator", 564, 1185 },
        { "Jorms Plush", 1086, 1447 },
        { "Jukebox", 1044, 1376 },
        { "Jungle Bush", 1141, 1458 },
        { "Jungle Log", 1126, 1479 },
        { "Jungle Log Arched", 1201, 1539 },
        { "Jungle Plant", 1104, 1517 },
        { "Kaineng City Umbrella", 562, -1 },
        { "Keep Construct Rubble", 590, 759 },
        { "Keg", 429, 460 },
        { "Keg Rack", 746, 388 },
        { "Kodan Bone Bookshelf", 622, -1 },
        { "Kodan Book: 20 Flavors of Sand", 914, -1 },
        { "Kodan Book: Glittering Gold", 936, -1 },
        { "Kodan Book: Glorious Green", 922, -1 },
        { "Kodan Book: Lofty Light Brown", 928, -1 },
        { "Kodan Book: Performative Puce", 921, -1 },
        { "Kodan Book: Soothing Sage", 937, -1 },
        { "Kodan Book: The Beauty of Blue", 926, -1 },
        { "Kodan Cottage Wall", 35, -1 },
        { "Kodan Cottage Wall (Pillar)", 570, -1 },
        { "Kodan Cottage Wall (Small)", 300, -1 },
        { "Kodan Crystal Candle", 312, -1 },
        { "Kodan Decorative Torch", 483, -1 },
        { "Kodan Decorative Wardrobe", 202, -1 },
        { "Kodan Dining Chair", 327, -1 },
        { "Kodan Fancy Bed", 225, -1 },
        { "Kodan Feast", 42, -1 },
        { "Kodan Fish Tank", 279, -1 },
        { "Kodan Hammock", 628, -1 },
        { "Kodan Hanging Lantern", 263, -1 },
        { "Kodan Heritage Chest", 315, -1 },
        { "Kodan Hero Statue", 21, -1 },
        { "Kodan Ivory Couch", 385, -1 },
        { "Kodan Joined Wooden Divider", 759, -1 },
        { "Kodan Kitchen Hearth", 727, -1 },
        { "Kodan Lantern", 565, -1 },
        { "Kodan Outdoor Bath", 146, -1 },
        { "Kodan Painting", 250, -1 },
        { "Kodan Painting 2", 573, -1 },
        { "Kodan Painting 3", 130, -1 },
        { "Kodan Refined End Table", 131, -1 },
        { "Kodan Rocky Fireplace", 732, -1 },
        { "Kodan Sink", 589, -1 },
        { "Kodan Soup Kettle", 586, -1 },
        { "Kodan Swirl Rug", 692, -1 },
        { "Kodan Table Setting", 224, -1 },
        { "Kodan Timber Stool", 419, -1 },
        { "Kodan Vanity", 306, -1 },
        { "Kodan Wall Mirror", 486, -1 },
        { "Kodan Woodsy Armchair", 610, -1 },
        { "Kodan Woodsy Cabinet", 70, -1 },
        { "Kodan Woodsy Cabinet (High)", 641, -1 },
        { "Kodan Woodsy Cabinet (Tall)", 436, -1 },
        { "Kodan Woodsy Dining Table", 613, -1 },
        { "Kodan Woodsy Trunk", 143, -1 },
        { "Kodan Woven Divider", 740, -1 },
        { "Kodan Writing Desk", 154, -1 },
        { "Kormir Statue", 400, 555 },
        { "Kournan Brazier", 552, 980 },
        { "Kryptis Bush", -1, 1268 },
        { "Kryptis Door (Tall and Wide)", 290, 1274 },
        { "Kryptis Goo Plane", 508, 1269 },
        { "Kryptis Pillar", 748, 1272 },
        { "Kryptis Platform", 266, 1271 },
        { "Kryptis Square Floor", 271, 1273 },
        { "Kryptis Stair", 456, 1270 },
        { "Kryptis Wall", 512, 1267 },
        { "Krytan Garden Column", 946, 1291 },
        { "Large Bamboo Clump", 463, -1 },
        { "Large Block of the Solid Ocean", 118, 780 },
        { "Large Canthan Cherry Tree", 669, -1 },
        { "Large Crystal Block of the Solid Ocean", 650, 766 },
        { "Large Cube of Snow", 389, 801 },
        { "Large Cuboid of Snow", 43, 800 },
        { "Large Elonian Planter", 726, -1 },
        { "Large Female Norn Holo-Dancer", 578, 784 },
        { "Large Festival Tent", 689, 248 },
        { "Large Hemisphere of Snow", 451, 795 },
        { "Large Male Norn Holo-Dancer", 348, 789 },
        { "Large Square Pillar", 24, 518 },
        { "Large Wave of the Solid Ocean", 760, 783 },
        { "Large Wedge of Snow", 771, 797 },
        { "Largos Platform", 1160, 1514 },
        { "Lattice", 648, 315 },
        { "Lattice Arbor", 135, 609 },
        { "Lattice Planter", 303, 252 },
        { "Lattice Planter with Blue Petunias", 46, 235 },
        { "Lattice Planter with Daisies", 778, 216 },
        { "Lattice Planter with Loosestrife", 591, 154 },
        { "Lattice Planter with Orange Petunias", 248, 627 },
        { "Lattice Planter with Red Petunias", 614, 199 },
        { "Leaping Lion Fountain", 86, -1 },
        { "Leaping Lion Statue", 416, 1229 },
        { "Library Shelf", 515, 528 },
        { "Light-Blue Wintersday Gift", 1078, 1449 },
        { "Lightning Aspect Crystal", 944, 1334 },
        { "Lion Fountain", 596, 1228 },
        { "Lion Statue", 713, 131 },
        { "Lit Wagon", 510, 295 },
        { "Loaded Wagon", 111, 143 },
        { "Long Fancy Table", 235, 644 },
        { "Lotus Rug", 877, -1 },
        { "Lounging Tiger Statue", 12, 1195 },
        { "Lunar Arch", 353, 698 },
        { "Lynx Statue", 723, 816 },
        { "Lyssa Statue", 443, 348 },
        { "Mad Moon", 100, 790 },
        { "Maddening Tower", 1156, 1457 },
        { "Majority Disciplines Research Commemorative Statue", 359, 592 },
        { "Male Norn Holo-Dancer", 9, 772 },
        { "Marriner Statue", 249, 492 },
        { "Massive Balloon Bouquet", 369, 190 },
        { "Mast Pole", 518, 459 },
        { "Master Brewer's Keg", 703, 703 },
        { "Mausoleum", 665, 793 },
        { "Meditative Bamboo Fence", 829, -1 },
        { "Meditative Bamboo Fountain", 835, -1 },
        { "Meditative Favored Rock", 834, -1 },
        { "Meditative Long Bamboo Fence", 827, -1 },
        { "Meditative Lucky Rock", 831, -1 },
        { "Meditative Sand Circle", 832, -1 },
        { "Meditative Sand Lines", 836, -1 },
        { "Meditative Stone Lantern", 826, -1 },
        { "Meditative Windswept Tree", 833, -1 },
        { "Melandru Statue", 22, 406 },
        { "Mimosa Tree", 1137, 1472 },
        { "Miniature Joko Bust (for Personal Worship)", 659, 918 },
        { "Miniature Majestic Joko (for Personal Worship)", 756, 929 },
        { "Mists Dolyak Statue", 408, 357 },
        { "Mists Drake Statue", 173, 412 },
        { "Mists Griffon Statue", 335, 171 },
        { "Mists Minotaur Statue", 638, 196 },
        { "Mists Rock Dog Statue", 407, 244 },
        { "Monkey Statue", 44, 684 },
        { "Mordremoth Mandible", 242, 714 },
        { "Mordy Plush", 1097, 1464 },
        { "Mossy Pillar", 754, 387 },
        { "Mounted Dolyak Head", 656, 815 },
        { "Mr. Quiggles", 861, -1 },
        { "Mushroom", 152, 587 },
        { "Mythwright Gambit Pillar", 1155, 1506 },
        { "Nautical String Lights", 960, 1413 },
        { "Netted Crates", 582, 1221 },
        { "Norn Brazier", 913, 1328 },
        { "Norn Stair", 933, 1329 },
        { "Norn Stone Structure", 919, 1327 },
        { "Norn Structure", 930, 1326 },
        { "Norn Summit Banner", 96, 639 },
        { "Norn Summit Flag", 646, 286 },
        { "Oakheart's Essence", 394, 846 },
        { "Obstacle: Blue Torch", 678, 547 },
        { "Obstacle: Chill Turret", 133, 477 },
        { "Obstacle: Cripple Turret", 807, 304 },
        { "Obstacle: Fan Trap", 114, 637 },
        { "Obstacle: Fear Turret", 18, 529 },
        { "Obstacle: Flame Turret", 543, 262 },
        { "Obstacle: Green Torch", 299, 447 },
        { "Obstacle: Heal Turret", 721, 425 },
        { "Obstacle: Knockback Turret", 14, 487 },
        { "Obstacle: Lava Floor", 179, 191 },
        { "Obstacle: Poison Turret", 801, 628 },
        { "Obstacle: Purple Torch", 198, 125 },
        { "Obstacle: Red Torch", 694, 435 },
        { "Obstacle: Strike Turret", 753, 197 },
        { "Obstacle: Tall Wall", 267, -1 },
        { "Obstacle: Vortex Trap", 59, 311 },
        { "Obstacle: Wall", 305, 166 },
        { "Obstacle: Weakness Turret", 433, 545 },
        { "Obstacle: White Torch", 37, 373 },
        { "Obstacle: Wide Lava Floor", 361, 527 },
        { "Obstacle: Wide Tall Wall", 569, -1 },
        { "Obstacle: Wide Wall", 244, 321 },
        { "Ominous Fortress Buttress", 953, 1339 },
        { "Ominous Fortress Ruins—Left", 951, 1337 },
        { "Ominous Fortress Ruins—Right", 948, 1338 },
        { "Ominous Fortress Wall", 550, 1074 },
        { "Ominous Fortress Wall: Angled", 712, 1071 },
        { "Ominous Fortress Wall: Corrupted", 147, 1072 },
        { "Orange Balloon", 685, 560 },
        { "Ornate Armor Stand", 286, 367 },
        { "Ornate Bed", 247, 973 },
        { "Ornate Grand Piano", 911, -1 },
        { "Orrax Contained", 927, -1 },
        { "Owl Spirit Statue", 676, 1162 },
        { "Ox Statue", 54, 1173 },
        { "Painting of Moto", 28, 722 },
        { "Palm Tree", 766, 404 },
        { "Partial Academic Arch", 20, 1258 },
        { "Partial Disciplines Research Commemorative Statue", 4, 355 },
        { "Peppermint Arrow", 480, 1138 },
        { "Peppermint Platform", 576, 1139 },
        { "Petite Statue of Joko the Immortal", 253, 933 },
        { "Pew", 618, 631 },
        { "Phoenix Lantern", 67, 965 },
        { "Pillar Candle", 585, 537 },
        { "Plain Red Lantern", 867, 1315 },
        { "Plain White Lantern", 869, 1316 },
        { "Planter Box", 387, -1 },
        { "Plush Armchair", 227, 172 },
        { "Plush Bear Head Pal", 817, -1 },
        { "Plush Bird Pal", 823, -1 },
        { "Plush Bunny Pal", 1094, -1 },
        { "Plush Chick Pal", 1090, -1 },
        { "Plush Choya Pal", 818, -1 },
        { "Plush Cuckoo Pal", 1092, -1 },
        { "Plush Ess", 1185, 1531 },
        { "Plush Quaggan Pal", 819, -1 },
        { "Plush Raptor Pal", 1093, -1 },
        { "Plush Rug", 471, 810 },
        { "Plush Sofa", 422, 431 },
        { "Plush Vulpine Pal", 1091, -1 },
        { "Portrait of Caudecus", 499, 831 },
        { "Portrait of Logan Thackeray", 172, 827 },
        { "Pot of Chrysanthemums", 71, 998 },
        { "Potted Bamboo", 700, 614 },
        { "Potted Bamboo Cluster", 495, 584 },
        { "Potted Blooming Moa Fern", 259, 141 },
        { "Potted Blue Moa Fern", 765, 243 },
        { "Potted Blue Orchid", 711, 473 },
        { "Potted Broad Paddlefrond", 153, 281 },
        { "Potted Croton", 707, 479 },
        { "Potted Cypress", 381, 426 },
        { "Potted Djinn's Tongue", 673, 337 },
        { "Potted Fan Palm", 169, 519 },
        { "Potted Fern Tree", 706, 496 },
        { "Potted Fruiting Night Thistle", 183, 465 },
        { "Potted Gold Fern", 36, 457 },
        { "Potted Jungle Grass", 523, 200 },
        { "Potted Junglerice", 600, 532 },
        { "Potted Lady Palm", 357, 553 },
        { "Potted Maguuma Lily", 191, 618 },
        { "Potted Maguuma Lily (Double Bloom)", 57, 456 },
        { "Potted Maguuma Lily (Triple Bloom)", 534, 551 },
        { "Potted Mature Night Thistle", 38, 265 },
        { "Potted Night Thistle", 606, 330 },
        { "Potted Night Thistle Bud", 601, 255 },
        { "Potted Paddlefrond", 598, 591 },
        { "Potted Palm", 405, 488 },
        { "Potted Petticoat Palm", 796, 436 },
        { "Potted Reaching Blue Fern", 626, 581 },
        { "Potted Reaching Gold Fern", 611, 575 },
        { "Potted Shaggy Palm", 29, 193 },
        { "Potted Shrub", 679, 463 },
        { "Potted Slender Fern Tree", 813, 149 },
        { "Potted Sprouting Night Thistle", 1, 206 },
        { "Potted Tall Cypress", 608, 525 },
        { "Potted Tree", 63, 409 },
        { "Primordial Leviathan Rib Cage", 104, 997 },
        { "Primordial Leviathan Rib Cage: Curved", 156, 996 },
        { "Primordial Leviathan Rib Cage: Left Curved", 556, 999 },
        { "Primordial Leviathan Rib Cage: Right Curved", 222, 1000 },
        { "Primordus Hologram Generator", 274, 1182 },
        { "Pulley Hook", 592, 1220 },
        { "Pumpkin", 367, 556 },
        { "Pumpkin Lanterns", 48, 1167 },
        { "Purple Balloon", 234, 414 },
        { "Purple Dragon Target", 355, 1058 },
        { "Qadim Pylon", 1140, 1482 },
        { "Qadim's Token", 137, 971 },
        { "Rabbit Statue", 182, 1233 },
        { "Racetrack Curved Ramp", 214, 1019 },
        { "Racetrack Downward Left Curve", 749, 1002 },
        { "Racetrack Downward Ramp", 420, 1017 },
        { "Racetrack Downward Right Curve", 672, 1015 },
        { "Racetrack Left Curve", 743, 1001 },
        { "Racetrack Long Straightaway", 793, 1018 },
        { "Racetrack Right Curve", 473, 1006 },
        { "Racetrack Straightaway", 117, 1007 },
        { "Racetrack Upward Left Curve", 468, 1012 },
        { "Racetrack Upward Ramp", 500, 1005 },
        { "Racetrack Upward Right Curve", 69, 1011 },
        { "Racing Checkpoint Projector", 775, 1210 },
        { "Racing Power-Up Projector", 301, 1216 },
        { "Ram Statue", 295, 709 },
        { "Raptor Rental Post", 434, 1265 },
        { "Rat Statue", 804, 1141 },
        { "Raven Hall", 929, 1330 },
        { "Raven Spirit Statue", 45, 1075 },
        { "Reaching Twisted Tree", 316, 1264 },
        { "Rec Room Floor Tile", 107, 813 },
        { "Rectangular Planter", 722, 218 },
        { "Red Balloon", 808, 400 },
        { "Red Bowl", 275, -1 },
        { "Red Bowl of Food", 276, -1 },
        { "Red Cushion", 7, 291 },
        { "Red Dragon Target", 189, 1053 },
        { "Red Festival Tent", 77, 699 },
        { "Red Festival Umbrella", 352, 680 },
        { "Red Flag", 616, 217 },
        { "Red Lantern", 58, 683 },
        { "Red Pirate Flag", 490, 195 },
        { "Red Throw Pillow", 226, 481 },
        { "Refined Streetlamp", 409, 453 },
        { "Reindeer Ice Sculpture", 693, 665 },
        { "Replica Castoran Mirror", 1021, -1 },
        { "Resurrection Tablet", 1139, 1456 },
        { "River of Souls Token", 258, 944 },
        { "Roller Beetle Boost Pad", 851, -1 },
        { "Roller Beetle Rental Post", 439, 1242 },
        { "Roof 720 x 200 x 240", 1113, 1466 },
        { "Roof 720 x 200 x 480", 1135, 1526 },
        { "Roof Piece", 1121, 1519 },
        { "Rooster Statue", 260, 804 },
        { "Roped Fence", 1027, 1403 },
        { "Round Academic Column", 349, 1255 },
        { "Round Elonian Windmill", 557, 904 },
        { "Round String Lights", 1015, 1420 },
        { "Row of Candles", 811, 531 },
        { "Rug", 554, 969 },
        { "Rustic Brazier", 241, 325 },
        { "Sabetha Flamethrower Fragment", 789, 677 },
        { "Samarog's Door", 1170, 1454 },
        { "Sandcastle", 1111, 1489 },
        { "Sandstone Pillar", 140, 369 },
        { "Scarecrow", 651, 645 },
        { "Scarred Ice Monolith", 860, 1276 },
        { "Scarred Ice Sheet", 863, 1275 },
        { "Scarred Ice Spike", 862, 1278 },
        { "Sea Raider Door", 901, -1 },
        { "Sea Raider Doorway", 908, -1 },
        { "Sea Raider Floor", 907, -1 },
        { "Sea Raider Gable", 906, -1 },
        { "Sea Raider Roof", 904, -1 },
        { "Sea Raider Roof Corner", 905, -1 },
        { "Sea Raider Statue", 902, -1 },
        { "Sea Raider Wall", 909, -1 },
        { "Sea Raider Window", 903, -1 },
        { "Seance Candle", 40, 318 },
        { "Season 1: Bronze Guild Challenger Trophy", 31, 657 },
        { "Season 1: Gold Guild Challenger Trophy", 121, 660 },
        { "Season 1: Platinum Guild Challenger Trophy", 11, 659 },
        { "Season 1: Silver Guild Challenger Trophy", 494, 656 },
        { "Season 2: Bronze Guild Challenger Trophy", 797, 716 },
        { "Season 2: Gold Guild Challenger Trophy", 282, 715 },
        { "Season 2: Platinum Guild Challenger Trophy", 205, 717 },
        { "Season 2: Silver Guild Challenger Trophy", 417, 718 },
        { "Season 3: Bronze Guild Challenger Trophy", 688, 737 },
        { "Season 3: Gold Guild Challenger Trophy", 285, 736 },
        { "Season 3: Platinum Guild Challenger Trophy", 228, 746 },
        { "Season 3: Silver Guild Challenger Trophy", 415, 735 },
        { "Season 4: Bronze Guild Challenger Trophy", 165, 740 },
        { "Season 4: Gold Guild Challenger Trophy", 66, 734 },
        { "Season 4: Platinum Guild Challenger Trophy", 322, 739 },
        { "Season 4: Silver Guild Challenger Trophy", 221, 741 },
        { "Season 5: Bronze Guild Challenger Trophy", 660, 747 },
        { "Season 5: Gold Guild Challenger Trophy", 399, 742 },
        { "Season 5: Platinum Guild Challenger Trophy", 441, 748 },
        { "Season 5: Silver Guild Challenger Trophy", 522, 743 },
        { "Season 6: Bronze Guild Challenger Trophy", 347, 744 },
        { "Season 6: Gold Guild Challenger Trophy", 621, 745 },
        { "Season 6: Platinum Guild Challenger Trophy", 780, 733 },
        { "Season 6: Silver Guild Challenger Trophy", 395, 738 },
        { "Seer Bed", 1216, 1548 },
        { "Seer Chest Large", 1197, 1547 },
        { "Seer Doorway", 1200, 1558 },
        { "Seer Floor", 1205, 1549 },
        { "Seer Lamp", 1212, 1540 },
        { "Seer Mirror Small", 1213, 1535 },
        { "Seer Painting", 1193, 1550 },
        { "Seer Pillar", 1210, 1541 },
        { "Seer Storage Cauldron", 1198, 1557 },
        { "Seer Table Large", 1195, 1555 },
        { "Seer Table Small", 1196, 1545 },
        { "Seer Wall", 1207, 1553 },
        { "Seitung Bridge", 5, 1245 },
        { "Shattered Stairway", 643, 1191 },
        { "Shatterer Crystal", 203, 685 },
        { "Shield Generator Siege", 298, 861 },
        { "Ship Bow", 1150, 1465 },
        { "Ship Mast", 1062, 1424 },
        { "Ship Stairs", 1042, 1371 },
        { "Ship Stern", 1118, 1510 },
        { "Ship Windmill", 1053, 1369 },
        { "Short Elonian Column", 539, 897 },
        { "Short Guild Banquet Table", 124, 554 },
        { "Short Guild Bar", 382, 470 },
        { "Siege Turtle Rental Post", 942, 1332 },
        { "Signal Lantern", 149, 896 },
        { "Significant Disciplines Research Commemorative Statue", 763, 137 },
        { "Silver Cairn the Indomitable Trophy", 634, 825 },
        { "Silver Cardinal Adina Trophy", 527, 1047 },
        { "Silver Cardinal Sabir Trophy", 243, 1045 },
        { "Silver Chak Gerent Trophy", 667, 712 },
        { "Silver Conjured Amalgamate Trophy", 421, 982 },
        { "Silver Decima Trophy", 844, 1305 },
        { "Silver Deimos Trophy", 337, 823 },
        { "Silver Desmina Trophy", 682, 940 },
        { "Silver Dhuum Trophy", 372, 932 },
        { "Silver Ether Djinn Trophy", 464, 1040 },
        { "Silver Gorseval Trophy", 97, 711 },
        { "Silver Greer Trophy", 842, 1308 },
        { "Silver Keep Construct Trophy", 80, 753 },
        { "Silver Mordremoth Trophy", 273, 694 },
        { "Silver Mursaat Overseer Trophy", 507, 807 },
        { "Silver Qadim Trophy", 180, 992 },
        { "Silver River of Souls Trophy", 668, 934 },
        { "Silver Sabetha Trophy", 92, 705 },
        { "Silver Samarog Trophy", 603, 811 },
        { "Silver Shatterer Trophy", 794, 704 },
        { "Silver Siege the Stronghold Trophy", 411, 755 },
        { "Silver Slothasor Trophy", 175, 671 },
        { "Silver Statue of Grenth Trophy", 123, 923 },
        { "Silver Tequatl Trophy", 627, 670 },
        { "Silver Triple Trouble Trophy", 53, 689 },
        { "Silver Twin Largos Trophy", 587, 970 },
        { "Silver Ura Trophy", 846, 1306 },
        { "Silver Vale Guardian Trophy", 815, 692 },
        { "Silver White Mantle Abomination Trophy", 595, 697 },
        { "Silver Xera Trophy", 768, 761 },
        { "Simple Brick Fireplace", 1003, 1359 },
        { "Simple Door", 984, 1349 },
        { "Simple Shelf", 237, 526 },
        { "Simple Table", 752, 239 },
        { "Simple Window", 970, 1358 },
        { "Skewed Stairway (Left)", 62, 1194 },
        { "Skewed Stairway (Right)", 144, 1193 },
        { "Skimmer Rental Post", 941, 1289 },
        { "Skyscale Rental Post", 935, 1286 },
        { "Slab of the Solid Ocean", 94, 787 },
        { "Slothasor Blue Mushroom", 1120, 1486 },
        { "Slothasor Mushroom", 637, 674 },
        { "Small Canthan Cherry Tree", 139, -1 },
        { "Small Elonian Planter", 404, -1 },
        { "Snake Statue", 865, 1317 },
        { "Snow Maker", 218, 666 },
        { "Snow Mound", 72, 950 },
        { "Snow Pile", 291, 661 },
        { "Snowfall Drift", 432, 1168 },
        { "Snowman Ice Sculpture", 120, 799 },
        { "Sparkle Light String", 859, 1310 },
        { "Sparse Drizzlewood Coast Tree", 448, 1151 },
        { "Sphere Topiary", 734, 475 },
        { "Spider's Web Floor", 373, 1227 },
        { "Spider's Web Wall", 761, 1225 },
        { "Spiral Elonian Windmill", 755, 885 },
        { "Spire of the Solid Ocean", 101, 777 },
        { "Spire Topiary", 87, 619 },
        { "Spooky Cauldron", 559, 194 },
        { "Spooky Dining Chair", 718, 909 },
        { "Spooky Dining Table", 6, 910 },
        { "Springer Rental Post", 864, 1277 },
        { "Square Academic Column", 690, 1248 },
        { "Square Cabinet", 308, 561 },
        { "Square Candlestick", 185, 510 },
        { "Square Firepit", 384, 480 },
        { "Square Guild Banquet Table", 412, 606 },
        { "Square Guild Bar", 733, 283 },
        { "Square Planter", 23, 312 },
        { "Squat Thorny Mushroom", 188, 405 },
        { "Stable Fence", 1159, 1524 },
        { "Stable Pen", 1117, 1505 },
        { "Stained Glass", 1167, 1498 },
        { "Stairs (120 x 120)", 1041, 1433 },
        { "Stairs (120 x 60)", 1030, 1379 },
        { "Stairs (Clockwise Corner)", 1057, 1432 },
        { "Star Light String", 856, 1313 },
        { "Statue of Grenth Token", 795, 935 },
        { "Statue of Joko the Fearsome", 209, 930 },
        { "Statue of Joko the Indomitable", 479, 928 },
        { "Statue of Joko the Majestic", 19, 936 },
        { "Statue of Joko the Powerful", 8, 927 },
        { "Statue of Joko the Regal", 174, 945 },
        { "Statue of Joko the Victorious", 418, 943 },
        { "Steel Pan", 213, -1 },
        { "Streets of Divinity's Reach", 278, 808 },
        { "Sturdy Outdoor Rug", 880, -1 },
        { "Stylized Stone Dragon Head", 584, 1201 },
        { "Summit Banner", 403, 136 },
        { "Summit Flag", 392, 485 },
        { "Sun Aspect Crystal", 939, 1335 },
        { "Super Angry Cloud", 677, 957 },
        { "Super Beech Tree", 438, 960 },
        { "Super Bridge Plank", 1187, 1530 },
        { "Super Campfire", 73, 839 },
        { "Super Cave Floor", 802, 1236 },
        { "Super Cave Pillar", 549, 1238 },
        { "Super Cave Plateau", 252, 1237 },
        { "Super Cave Stalagmite", 364, 1235 },
        { "Super Chest", 84, 959 },
        { "Super Cliff Face", 435, 1029 },
        { "Super Cloud", 27, 721 },
        { "Super Cloud Wall", 697, 1207 },
        { "Super Flower", 617, 833 },
        { "Super Forest House", 13, 837 },
        { "Super Giant Cloud", 604, 1206 },
        { "Super Grand Gate", 893, 1283 },
        { "Super Grumpy Cloud", 493, 1205 },
        { "Super Happy Cloud", 496, 961 },
        { "Super Ice Wall", 762, 1031 },
        { "Super King Frog", 231, 729 },
        { "Super Large Rock", 170, 958 },
        { "Super Leaf Platform", 257, 1147 },
        { "Super Lily Pad", 81, 1150 },
        { "Super Lord Vanquish Banner", 1192, -1 },
        { "Super Lord Vanquish Brazier", 1191, -1 },
        { "Super Lord Vanquish Rug", 1183, -1 },
        { "Super Lord Vanquish Statue", 1184, -1 },
        { "Super Lord Vanquish Thin Wall", 1186, -1 },
        { "Super Lord Vanquish Wall", 1190, -1 },
        { "Super Mountain", 47, 725 },
        { "Super Mushroom", 624, 836 },
        { "Super Mushroom Platform", 204, 1148 },
        { "Super Owl Statue", 889, 1282 },
        { "Super Pagoda Arch", 898, 1320 },
        { "Super Pagoda Column", 741, 719 },
        { "Super Pagoda Deck", 895, 1325 },
        { "Super Pagoda Door", 891, 1318 },
        { "Super Pagoda Floor", 890, 1323 },
        { "Super Pagoda Long Roof", 899, 1321 },
        { "Super Pagoda Pond", 900, 1324 },
        { "Super Pagoda Roof", 892, 1319 },
        { "Super Pagoda Wall", 897, 1322 },
        { "Super Pine Tree", 593, 843 },
        { "Super Pointing Flat Glad Hand", 256, 1178 },
        { "Super Pointing Flat Hand", 764, 1176 },
        { "Super Pointing Flat Sad Hand", 791, 1175 },
        { "Super Pointing Glad Hand", 262, 1179 },
        { "Super Pointing Hand", 425, 1177 },
        { "Super Pointing Sad Hand", 356, 1180 },
        { "Super Rainbow Arch", 90, 1204 },
        { "Super Rapids Flowing Water", 1182, 1532 },
        { "Super Rapids Pillar", 1189, 1533 },
        { "Super Rapids Plateau", 1181, 1529 },
        { "Super Red Crystal", 894, 1281 },
        { "Super Rock", 528, 727 },
        { "Super Rock Platform", 568, 723 },
        { "Super Rock Ramp", 548, 730 },
        { "Super Rock Wall", 330, 724 },
        { "Super Short Cliff Face", 599, 1028 },
        { "Super Small Rock", 631, 728 },
        { "Super Snow Floor", 103, 1033 },
        { "Super Snowy Cliff Face", 115, 1032 },
        { "Super Tall Cliff Face", 607, 1030 },
        { "Super Tree", 714, 720 },
        { "Super Tree Branch", 782, 1146 },
        { "Super Tree Trunk", 481, 1149 },
        { "Superior Arrow Cart Siege", 644, 853 },
        { "Superior Ballista Siege", 329, 855 },
        { "Superior Catapult Siege", 378, 859 },
        { "Superior Flame Ram Siege", 478, 847 },
        { "Superior Golem Siege", 268, 848 },
        { "Superior Shield Generator Siege", 580, 865 },
        { "Superior Trebuchet Siege", 157, 862 },
        { "Sylvari Summit Banner", 470, 145 },
        { "Sylvari Summit Flag", 505, 617 },
        { "Tall Academic Arch", 85, 1250 },
        { "Tall Candlestick", 653, 326 },
        { "Tall Elonian Column", 506, 876 },
        { "Tall Lattice", 705, 297 },
        { "Tall Ship Stairs", 1023, 1439 },
        { "Tea Set", 184, -1 },
        { "Teleporter Entrance", 636, 974 },
        { "Teleporter Exit", 33, 988 },
        { "Tequatl Tailbone", 729, 681 },
        { "The Heart of the Priory", 708, 814 },
        { "Thick Drizzlewood Coast Tree", 566, 1153 },
        { "Thin Candlestick", 65, 134 },
        { "Thorny Jack-o'-Lantern", 670, 534 },
        { "Thorny Mushroom", 122, 578 },
        { "Throne", 521, 139 },
        { "Tiger Statue", 410, 1199 },
        { "Toymaker's Machine", 129, 1196 },
        { "Traditional Kodan Bed", 125, -1 },
        { "Tranquil Birdbath", 530, -1 },
        { "Tray of Eggnog", 555, 955 },
        { "Trebuchet Siege", 108, 864 },
        { "Trimmed Wall (960 x 240)", 990, 1372 },
        { "Triple Trouble Tooth", 792, 690 },
        { "Triumphant Dragon Bash Poster", 632, 1159 },
        { "Tropical Bridge", 1004, 1410 },
        { "Tropical Bush", 1063, 1356 },
        { "Tropical Bush (Tall)", 1038, 1345 },
        { "Tropical Shrub", 1032, 1381 },
        { "Twin Largos' Token", 49, 981 },
        { "Twisted Imaginarium Cabinet", 873, -1 },
        { "Twisted Imaginarium Choya Statue", 875, -1 },
        { "Twisted Imaginarium Entryway", 872, -1 },
        { "Twisted Imaginarium Mushroom", 874, -1 },
        { "Twisted Imaginarium Table", 871, -1 },
        { "Typical Drizzlewood Coast Tree", 83, 1152 },
        { "Tyrian Globe", 76, 579 },
        { "Unchained Platform", 655, 1192 },
        { "Underwater Anemone", 1072, -1 },
        { "Underwater Coral Formation", 1071, -1 },
        { "Underwater Frenzied Fish", 1068, -1 },
        { "Underwater Geothermal Vent", 1075, -1 },
        { "Underwater Jellyfish", 1076, -1 },
        { "Underwater Kelp", 1070, -1 },
        { "Underwater Kelp Grove", 1074, -1 },
        { "Underwater Meandering Fish", 1066, -1 },
        { "Underwater Porous Rock", 1069, -1 },
        { "Underwater Solid Rock", 1073, -1 },
        { "Underwater Tranquil Fish", 1067, -1 },
        { "Unimpressive King Frog", 379, 726 },
        { "Ura's Token (Guild Decoration)", -1, 1309 },
        { "Ura's Token (Homestead)", 845, -1 },
        { "Utensil Rack", 715, -1 },
        { "Uzolan's Mechanical Orchestra", 142, 340 },
        { "Vale Guardian Pieces", 246, 675 },
        { "Vale Guardian Pylon", 1127, 1477 },
        { "Verdant Door", 1173, -1 },
        { "Verdant Doorway", 1175, -1 },
        { "Verdant Gable", 1171, -1 },
        { "Verdant Light", 1179, -1 },
        { "Verdant Roof", 1178, -1 },
        { "Verdant Stairs", 1177, -1 },
        { "Verdant Wall", 1172, -1 },
        { "Verdant Window", 1176, -1 },
        { "Victorious Dragon Bash Poster", 82, 1160 },
        { "Vined Lattice", 662, 549 },
        { "Visage of Madness", 472, 1073 },
        { "Wagon", 2, 511 },
        { "Wall (120 x 120)", 1049, 1387 },
        { "Wall (120 x 240)", 997, 1419 },
        { "Wall (120 x 60)", 973, 1362 },
        { "Wall (240 x 120)", 976, 1418 },
        { "Wall (240 x 240)", 1050, 1425 },
        { "Wall (60 x 120)", 1060, 1422 },
        { "Wall (60 x 240)", 1061, 1365 },
        { "Wall (600 x 240)", 974, 1390 },
        { "Wall (960 x 240)", 1006, 1340 },
        { "Wall Bracket Corner", 982, 1438 },
        { "Wall Bracket Half (480)", 992, 1361 },
        { "Wall Bracket Half (960)", 1029, 1341 },
        { "Wall Gable 360 x 100", 1138, 1508 },
        { "Wall Gable 360 x 100 Mirror", 1112, 1500 },
        { "Wall Gable 360 x 200", 1149, 1473 },
        { "Wall Gable 360 x 200 Mirror", 1109, 1512 },
        { "Wall Gable 720 x 100", 1158, 1516 },
        { "Wall Gable 720 x 200", 1099, 1481 },
        { "Wall Window 120 x 240", 1124, 1470 },
        { "Warclaw Rental Post", 828, 1293 },
        { "Wave of the Solid Ocean", 93, 771 },
        { "Wayfinder's Versatile Board Game Table", 820, -1 },
        { "Wayfinder's Versatile Navigation Table", 821, -1 },
        { "Wayfinder's Versatile Plain Table", 824, -1 },
        { "Wayfinder's Versatile Table", 375, -1 },
        { "Wayfinder's Versatile War Table", 822, -1 },
        { "Weapon Display Case", 720, -1 },
        { "Weathered Elonian Arch", 450, 890 },
        { "Weathered Elonian Column", 79, 880 },
        { "Weathered Elonian Obelisk", 346, 888 },
        { "Wedge of Snow", 671, 803 },
        { "Well", 431, 455 },
        { "Whale Hologram", 915, -1 },
        { "White Balloon", 816, 403 },
        { "White Flag", 731, 254 },
        { "White Mantle Abomination Crystal", 216, 687 },
        { "White Mantle Statue", 1101, 1490 },
        { "White Mantle Turret", 1103, 1496 },
        { "White Wintersday Gift", 181, 954 },
        { "Wide Entryway Wall (Single)", 1033, 1347 },
        { "Wide Library Shelf", 777, 241 },
        { "Wind Aspect Crystal", 938, 1331 },
        { "Windmill Wheel", 430, 1223 },
        { "Window", 1048, 1407 },
        { "Windowed Wall (600 x 240)", 994, 1391 },
        { "Windowed Wall (960 x 240)", 1046, 1399 },
        { "Wintersday Garland", 535, 1171 },
        { "Wintersday Music Platform", 686, 1197 },
        { "Wintersday Star", 567, 1169 },
        { "Wintersday Tree", 206, 662 },
        { "Wizard Rug", 881, -1 },
        { "Wooden Beam (16 x 120 x 16)", 999, 1373 },
        { "Wooden Beam (16 x 120 x 30)", 1031, 1353 },
        { "Wooden Beam (16 x 240 x 16)", 977, 1431 },
        { "Wooden Beam (16 x 240 x 30)", 1009, 1421 },
        { "Wooden Beam (16 x 480 x 16)", 1051, 1445 },
        { "Wooden Beam (16 x 480 x 30)", 959, 1434 },
        { "Wooden Beam Corner (30 x 120 x 30)", 993, 1416 },
        { "Wooden Beam Corner (30 x 240 x 30)", 1007, 1380 },
        { "Woodfire Grill", 701, 607 },
        { "Worn Arch", 296, 228 },
        { "Worn Pillar", 119, 569 },
        { "Writhing Twisted Tree", 800, 1263 },
        { "Wrought Iron Dragon Body", 910, 1285 },
        { "Wrought Iron Dragon Statue", 934, 1288 },
        { "Wrought Iron Dragon Tail", 923, 1287 },
        { "WvW Mortar Siege", 134, 851 },
        { "Xera's Ribbon Scrap", 89, 752 },
        { "Yellow Balloon", 619, 508 },
        { "Yellow Cushion", 383, 276 },
        { "Zephyr Banner", 605, 1189 },
        { "Zephyr Bridge", 544, 966 },
        { "Zephyr Canopy", 317, 1188 },
        { "Zephyr Koi Lantern", 281, 1061 },
        { "Zephyr Lantern", 390, 964 },
        { "Zephyr Net", 223, 1190 },
        { "Zephyr Rowboat", 516, 1062 },
        { "Zephyr Sail", 699, 963 },
        { "Zephyr Sailboat", 230, 1060 },
        { "Zephyr Scaffolding", 338, 1158 },
        { "Zephyr Support", 362, 1157 },
        { "Zephyr Walkway", 311, 1155 },
        { "Zephyr Waterpot", 297, 1059 },
        { "Zephyr Window Pane", 461, 962 },
        { "Zephyrite Display Stand", 940, 1333 },
    };

    std::vector<DecorationDatabase::Entry> activeEntries;
    std::string databasePath;
    struct CatalogResult
    {
        bool success = false;
        std::string error;
        std::vector<Gw2Api::HomesteadDecorationDefinition> definitions;
    };
    std::future<CatalogResult> catalogJob;
    bool catalogUpdateRunning = false;

    std::string FoldAscii(std::string value)
    {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character)
        {
            return static_cast<char>(character < 128 ? std::tolower(character) : character);
        });
        return value;
    }

    std::string CleanDatabaseName(std::string value)
    {
        const size_t lineBreak = value.find_first_of("\r\n");
        if (lineBreak != std::string::npos)
        {
            value.erase(lineBreak);
        }

        while (true)
        {
            const size_t tagStart = value.find('<');
            if (tagStart == std::string::npos)
            {
                break;
            }

            const size_t tagEnd = value.find('>', tagStart + 1);
            if (tagEnd == std::string::npos)
            {
                break;
            }
            value.erase(tagStart, tagEnd - tagStart + 1);
        }

        std::string output;
        bool pendingSpace = false;
        for (const char character : value)
        {
            if (std::isspace(static_cast<unsigned char>(character)) != 0)
            {
                pendingSpace = !output.empty();
            }
            else
            {
                if (pendingSpace) output += ' ';
                output += character;
                pendingSpace = false;
            }
        }
        return output;
    }

    std::string JsonEscape(const std::string& value)
    {
        std::string output;
        output.reserve(value.size());
        for (const unsigned char character : value)
        {
            switch (character)
            {
            case '\\': output += "\\\\"; break;
            case '"': output += "\\\""; break;
            case '\b': output += "\\b"; break;
            case '\f': output += "\\f"; break;
            case '\n': output += "\\n"; break;
            case '\r': output += "\\r"; break;
            case '\t': output += "\\t"; break;
            default: output += static_cast<char>(character); break;
            }
        }
        return output;
    }

    size_t FindQuotedBoundary(
        const std::string& text,
        size_t start,
        char opening,
        char closing
    )
    {
        int depth = 0;
        bool quoted = false;
        bool escaped = false;

        for (size_t index = start; index < text.size(); ++index)
        {
            const char character = text[index];
            if (quoted)
            {
                if (escaped) escaped = false;
                else if (character == '\\') escaped = true;
                else if (character == '"') quoted = false;
                continue;
            }

            if (character == '"') quoted = true;
            else if (character == opening) ++depth;
            else if (character == closing && --depth == 0) return index;
        }

        return std::string::npos;
    }

    size_t FindJsonValue(
        const std::string& json,
        const char* key,
        size_t begin,
        size_t end
    )
    {
        const std::string token = std::string("\"") + key + "\"";
        size_t position = json.find(token, begin);
        if (position == std::string::npos || position >= end)
        {
            return std::string::npos;
        }

        position = json.find(':', position + token.size());
        if (position == std::string::npos || position >= end)
        {
            return std::string::npos;
        }

        return json.find_first_not_of(" \t\r\n", position + 1);
    }

    std::string ReadJsonString(
        const std::string& json,
        const char* key,
        size_t begin,
        size_t end
    )
    {
        size_t position = FindJsonValue(json, key, begin, end);
        if (position == std::string::npos || position >= end || json[position] != '"')
        {
            return {};
        }

        std::string value;
        bool escaped = false;
        for (++position; position < end; ++position)
        {
            const char character = json[position];
            if (escaped)
            {
                switch (character)
                {
                case 'b': value += '\b'; break;
                case 'f': value += '\f'; break;
                case 'n': value += '\n'; break;
                case 'r': value += '\r'; break;
                case 't': value += '\t'; break;
                default: value += character; break;
                }
                escaped = false;
            }
            else if (character == '\\') escaped = true;
            else if (character == '"') return value;
            else value += character;
        }

        return {};
    }

    int ReadJsonId(
        const std::string& json,
        const char* key,
        size_t begin,
        size_t end
    )
    {
        const size_t position = FindJsonValue(json, key, begin, end);
        if (position == std::string::npos || position >= end ||
            json.compare(position, 4, "null") == 0)
        {
            return -1;
        }

        try
        {
            return std::stoi(json.substr(position, end - position));
        }
        catch (...)
        {
            return -1;
        }
    }

    bool LoadDatabase(
        const std::string& path,
        std::vector<DecorationDatabase::Entry>& loaded
    )
    {
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open())
        {
            return false;
        }

        std::ostringstream contents;
        contents << file.rdbuf();
        const std::string json = contents.str();
        const size_t decorationsKey = json.find("\"Decorations\"");
        const size_t arrayBegin = decorationsKey == std::string::npos
            ? std::string::npos
            : json.find('[', decorationsKey);
        if (arrayBegin == std::string::npos)
        {
            return false;
        }

        const size_t arrayEnd = FindQuotedBoundary(json, arrayBegin, '[', ']');
        if (arrayEnd == std::string::npos)
        {
            return false;
        }

        size_t position = arrayBegin + 1;
        while (position < arrayEnd)
        {
            const size_t objectBegin = json.find('{', position);
            if (objectBegin == std::string::npos || objectBegin >= arrayEnd)
            {
                break;
            }

            const size_t objectEnd = FindQuotedBoundary(json, objectBegin, '{', '}');
            if (objectEnd == std::string::npos || objectEnd > arrayEnd)
            {
                return false;
            }

            DecorationDatabase::Entry entry;
            entry.name = CleanDatabaseName(
                ReadJsonString(json, "Name", objectBegin, objectEnd)
            );
            entry.homesteadId =
                ReadJsonId(json, "HomesteadId", objectBegin, objectEnd);
            entry.guildUpgradeId =
                ReadJsonId(json, "GuildUpgradeId", objectBegin, objectEnd);
            entry.maxCount =
                ReadJsonId(json, "MaxCount", objectBegin, objectEnd);

            if (!entry.name.empty() &&
                (entry.homesteadId > 0 || entry.guildUpgradeId > 0))
            {
                loaded.push_back(std::move(entry));
            }

            position = objectEnd + 1;
        }

        return !loaded.empty();
    }

    bool SaveDatabase(
        const std::string& path,
        const std::vector<DecorationDatabase::Entry>& entries
    )
    {
        const std::filesystem::path finalPath(path);
        const std::filesystem::path temporaryPath =
            finalPath.parent_path() / (finalPath.filename().string() + ".tmp");

        std::ofstream file(temporaryPath, std::ios::binary | std::ios::trunc);
        if (!file.is_open())
        {
            return false;
        }

        std::set<int> guildIds;
        std::set<int> homesteadIds;
        for (const DecorationDatabase::Entry& entry : entries)
        {
            if (entry.guildUpgradeId > 0) guildIds.insert(entry.guildUpgradeId);
            if (entry.homesteadId > 0) homesteadIds.insert(entry.homesteadId);
        }

        const std::time_t now = std::time(nullptr);
        std::tm utc = {};
        gmtime_s(&utc, &now);

        file << "{\n";
        file << "  \"Version\": 2,\n";
        file << "  \"GeneratedAtUtc\": \""
            << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ") << "\",\n";
        file << "  \"GeneratedBy\": \"Pewpew's Deco Tools 1.3.2.3\",\n";
        file << "  \"SourceSnapshot\": {\n";
        file << "    \"GuildUpgradeIds\": [";
        size_t written = 0;
        for (const int id : guildIds)
        {
            if (written++ > 0) file << ", ";
            file << id;
        }
        file << "],\n";
        file << "    \"HomesteadDecorationIds\": [";
        written = 0;
        for (const int id : homesteadIds)
        {
            if (written++ > 0) file << ", ";
            file << id;
        }
        file << "]\n";
        file << "  },\n";
        file << "  \"Decorations\": [\n";
        for (size_t index = 0; index < entries.size(); ++index)
        {
            const DecorationDatabase::Entry& entry = entries[index];
            file << "    {\n";
            file << "      \"Name\": \"" << JsonEscape(entry.name) << "\",\n";
            file << "      \"HomesteadId\": ";
            if (entry.homesteadId > 0) file << entry.homesteadId;
            else file << "null";
            file << ",\n";
            file << "      \"GuildUpgradeId\": ";
            if (entry.guildUpgradeId > 0) file << entry.guildUpgradeId;
            else file << "null";
            file << ",\n";
            file << "      \"MaxCount\": ";
            if (entry.maxCount >= 0) file << entry.maxCount;
            else file << "null";
            file << "\n";
            file << "    }" << (index + 1 < entries.size() ? "," : "") << "\n";
        }
        file << "  ]\n";
        file << "}\n";
        file.close();

        if (!file.good())
        {
            std::error_code cleanupError;
            std::filesystem::remove(temporaryPath, cleanupError);
            return false;
        }

        std::error_code error;
        std::filesystem::remove(finalPath, error);
        error.clear();
        std::filesystem::rename(temporaryPath, finalPath, error);
        return !error;
    }

    bool NeedsCatalogUpdate()
    {
        return std::any_of(
            activeEntries.begin(),
            activeEntries.end(),
            [](const DecorationDatabase::Entry& entry)
            {
                return entry.homesteadId > 0 && entry.maxCount < 0;
            }
        );
    }

    void StartCatalogUpdate()
    {
        if (catalogUpdateRunning || !NeedsCatalogUpdate()) return;
        catalogUpdateRunning = true;
        catalogJob = std::async(std::launch::async, []()
        {
            CatalogResult result;
            result.success = Gw2Api::LoadHomesteadDecorationDefinitions(
                result.definitions,
                result.error
            );
            return result;
        });
    }

    void ApplyCatalogResult(CatalogResult&& result)
    {
        if (!result.success) return;
        for (const Gw2Api::HomesteadDecorationDefinition& definition : result.definitions)
        {
            auto found = std::find_if(
                activeEntries.begin(),
                activeEntries.end(),
                [&definition](const DecorationDatabase::Entry& entry)
                {
                    return entry.homesteadId == definition.id;
                }
            );
            if (found == activeEntries.end())
            {
                activeEntries.push_back(
                    { definition.name, definition.id, -1, definition.maxCount }
                );
            }
            else
            {
                if (!definition.name.empty()) found->name = definition.name;
                found->maxCount = definition.maxCount;
            }
        }
        std::sort(
            activeEntries.begin(),
            activeEntries.end(),
            [](const DecorationDatabase::Entry& left, const DecorationDatabase::Entry& right)
            {
                return left.name < right.name;
            }
        );
        SaveDatabase(databasePath, activeEntries);
    }
}

void DecorationDatabase::Initialize(const std::string& addonDirectory)
{
    databasePath = (
        std::filesystem::path(addonDirectory) /
        "decorations.db.json"
    ).string();

    std::vector<Entry> loaded;
    if (LoadDatabase(databasePath, loaded))
    {
        activeEntries = std::move(loaded);
        StartCatalogUpdate();
        return;
    }

    activeEntries.assign(std::begin(Entries), std::end(Entries));
    SaveDatabase(databasePath, activeEntries);
    StartCatalogUpdate();
}

void DecorationDatabase::Update()
{
    if (!catalogUpdateRunning || !catalogJob.valid() ||
        catalogJob.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
    {
        return;
    }
    CatalogResult result;
    try { result = catalogJob.get(); }
    catch (...) {}
    catalogUpdateRunning = false;
    ApplyCatalogResult(std::move(result));
}

void DecorationDatabase::Shutdown()
{
    if (catalogJob.valid())
    {
        CatalogResult result;
        try { result = catalogJob.get(); }
        catch (...) {}
        catalogUpdateRunning = false;
        ApplyCatalogResult(std::move(result));
    }
    activeEntries.clear();
    databasePath.clear();
}

const DecorationDatabase::Entry* DecorationDatabase::FindByCleanName(const std::string& cleanName)
{
    const std::string target = FoldAscii(cleanName);
    for (const Entry& entry : activeEntries)
    {
        if (FoldAscii(entry.name) == target)
        {
            return &entry;
        }
    }
    return nullptr;
}

const char* DecorationDatabase::FindNameById(int id, int type)
{
    for (const Entry& entry : activeEntries)
    {
        if ((type == 0 ? entry.homesteadId : entry.guildUpgradeId) == id)
        {
            return entry.name.c_str();
        }
    }
    return nullptr;
}

int DecorationDatabase::FindMaxCountById(int id, int type)
{
    if (type != 0) return -1;
    for (const Entry& entry : activeEntries)
    {
        if (entry.homesteadId == id)
        {
            return entry.maxCount;
        }
    }
    return -1;
}

int DecorationDatabase::Count()
{
    return static_cast<int>(activeEntries.size());
}

const std::string& DecorationDatabase::GetPath()
{
    return databasePath;
}
