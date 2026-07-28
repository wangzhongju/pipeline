#!/bin/bash

# Record the start time of the script
start_time=$(date +%s)

while true; do
    # Print the temperature command and its result
    echo "Command: cat /sys/class/hwmon/hwmon0/temp1_input"
    temp_result=$(cat /sys/class/hwmon/hwmon0/temp1_input 2>/dev/null)
    if [ $? -eq 0 ]; then
        echo "Result: $temp_result"
    else
        echo "Result: Failed to read temperature"
    fi

    # Print the memory command and its result
    echo "Command: free --mega"
    free_result=$(free --mega)
    echo "Result:"
    echo "$free_result"

    # Calculate and display the elapsed time since the script started
    current_time=$(date +%s)
    elapsed_time=$((current_time - start_time))
    echo "Script has been running for $elapsed_time seconds."

    # Add two newlines to separate the loops
    echo -e "\n\n"

    # Sleep for 20 seconds before the next iteration
    sleep 20
done