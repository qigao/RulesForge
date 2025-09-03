package com.example.drillsandroidapp

import android.os.Bundle
import android.widget.Button
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity

class MainActivity : AppCompatActivity() {

    private lateinit var outputTextView: TextView
    private val drillsEngine = DrillsEngine()

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)

        outputTextView = findViewById(R.id.outputTextView)

        findViewById<Button>(R.id.initEngineButton).setOnClickListener {
            appendOutput("Initializing engine...")
            val result = drillsEngine.initEngine()
            appendOutput(result)
        }

        findViewById<Button>(R.id.loadRulesButton).setOnClickListener {
            appendOutput("Loading rules...")
            val drlRules = """
                package com.example

                rule "Greeting Rule"
                when
                    $person : Person(name == "Alice")
                then
                    System.out.println("Hello, " + $person.name + "!");
                    // You can also modify facts or insert new ones here
                end
                """.trimIndent()
            val result = drillsEngine.loadRules(drlRules)
            appendOutput(result)
        }

        findViewById<Button>(R.id.insertFactButton).setOnClickListener {
            appendOutput("Inserting fact...")
            val factJson = "{\"name\": \"Alice\", \"age\": 30}"
            val result = drillsEngine.insertFact(factJson)
            appendOutput(result)
        }

        findViewById<Button>(R.id.fireRulesButton).setOnClickListener {
            appendOutput("Firing rules...")
            val result = drillsEngine.fireAllRules()
            appendOutput(result)
        }

        findViewById<Button>(R.id.destroyEngineButton).setOnClickListener {
            appendOutput("Destroying engine...")
            val result = drillsEngine.destroyEngine()
            appendOutput(result)
        }
    }

    private fun appendOutput(message: String) {
        runOnUiThread { // Ensure UI updates on the main thread
            outputTextView.append("$message\n")
            // Optionally scroll to the bottom
            // val scrollView = findViewById<ScrollView>(R.id.scrollView)
            // scrollView.post { scrollView.fullScroll(View.FOCUS_DOWN) }
        }
    }

    override fun onDestroy() {
        super.onDestroy()
        // Ensure engine is destroyed when activity is destroyed
        drillsEngine.destroyEngine()
    }
}
