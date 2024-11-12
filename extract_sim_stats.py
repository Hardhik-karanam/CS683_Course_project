import sqlite3
import csv

# Define the path to the SQLite database and output CSV file
input_db = '/home/saiteja/cs683/project/sniper/Virtuoso/sim.stats.sqlite3'
output_file = 'sim_stats.csv'

# Connect to the SQLite database
conn = sqlite3.connect(input_db)
cursor = conn.cursor()

# Get the list of tables in the database
cursor.execute("SELECT name FROM sqlite_master WHERE type='table';")
tables = cursor.fetchall()

# Iterate over each table and export it to CSV
for table_name in tables:
    table_name = table_name[0]
    # Query all data from the table, enclosing the table name in double quotes
    cursor.execute("SELECT * FROM \"{}\"".format(table_name))
    rows = cursor.fetchall()

    # Get column names
    column_names = [description[0] for description in cursor.description]

    # Write to CSV
    with open('{}_{}'.format(table_name, output_file), 'w') as csvfile:
        csvwriter = csv.writer(csvfile)
        csvwriter.writerow(column_names)  # Write headers
        csvwriter.writerows(rows)         # Write data

    print("Data from table '{}' has been written to {}_{}".format(table_name, table_name, output_file))

# Close the database connection
conn.close()

