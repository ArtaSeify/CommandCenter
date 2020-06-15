# CommandCenter: AI Bot for Broodwar and Starcraft II

Please see original CommandCenter for help on building the bot.

This version of the bot combines CommandCenter with BOSS (Build Order Search System). The initial build order is still written in the .json file. Once this build order is finished, the rest of the build orders are provided by BOSS. This is accomplished inside the BOSSManager class. BOSS is run on a separate thread, so planning occurs as the bot plays the game.

As BOSS is not public, it is not available to clone from this repository. If interested in the search system, please send me an email arta.seify@gmail.com.
