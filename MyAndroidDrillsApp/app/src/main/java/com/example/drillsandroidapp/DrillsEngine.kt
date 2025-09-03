package com.example.drillsandroidapp

class DrillsEngine {

    init {
        System.loadLibrary("drills_capi") // Load the drills_capi shared library
        System.loadLibrary("native-lib") // Load the native-lib shared library
    }

    /**
     * Initializes the Drills rule engine.
     * @return A status message indicating success or failure.
     */
    external fun initEngine(): String

    /**
     * Loads DRL rules into the engine.
     * @param drlRules The DRL rules as a string.
     * @return A status message indicating success or failure.
     */
    external fun loadRules(drlRules: String): String

    /**
     * Inserts a fact into the engine.
     * @param factJson The fact as a JSON string.
     * @return A status message indicating success or failure.
     */
    external fun insertFact(factJson: String): String

    /**
     * Fires all rules in the engine.
     * @return A status message indicating the number of rules fired or an error.
     */
    external fun fireAllRules(): String

    /**
     * Destroys the Drills rule engine instance.
     * @return A status message indicating success or failure.
     */
    external fun destroyEngine(): String
}
