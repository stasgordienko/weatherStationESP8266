typedef struct {
	int unsigned dd;
	int unsigned hh;
	int unsigned mm;

	float windSpeed_min;
	float windSpeed_max;
	float windSpeed_avg;

	int windDirection_min;
	int windDirection_max;
	int windDirection_avg;

	float pressure_avg;
	float tempBMP_avg;
	float humidity_avg;
	float tempDHT_avg;
	
	float sunny;
	float batt;
} Weather;
