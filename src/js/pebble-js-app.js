var initialised = false;
var CityID = 0, posLat = "0", posLon = "0", lang = "en", colorpbl=0;
var weatherIcon = {
    "01d" : 'I',
    "02d" : '"',
    "03d" : '!',
    "04d" : 'k',
    "09d" : '$',
    "10d" : '+',
    "11d" : 'F',
    "13d" : '9',
    "50d" : '=',
    "01n" : 'N',
    "02n" : '#',
    "03n" : '!',
    "04n" : 'k',
    "09n" : '$',
    "10n" : ',',
    "11n" : 'F',
    "13n" : '9',
    "50n" : '>'
};

//-----------------------------------------------------------------------------------------------------------------------
Pebble.addEventListener("ready", function() {
    initialised = true;
	
	var p_lang = "en_US";

	//Get pebble language
	if(Pebble.getActiveWatchInfo) {
		try {
			var watch = Pebble.getActiveWatchInfo();
			p_lang = watch.language;
			if (watch.platform === 'basalt')
				colorpbl = 1;
		} catch(err) {
			console.log("Pebble.getActiveWatchInfo(); Error!");
		}
	} 

	//Choose language
	var sub = p_lang.substring(0, 2);
	if (sub === "de")
		lang = "de";
	else  if (sub === "es")
		lang = "es";
	else if (sub === "fr")
		lang = "fr";
	else if (sub === "it")
		lang = "it";
	else
		lang = "en";
	
	console.log("JavaScript app ready and running! Lang: "+lang+", ColorPebble: "+colorpbl);
	sendMessageToPebble({"JS_READY": 1});		
});
//-----------------------------------------------------------------------------------------------------------------------
function sendMessageToPebble(payload) {
	Pebble.sendAppMessage(payload, 
		function(e) {
			console.log('Successfully delivered message (' + e.payload + ') with transactionId='+ e.data.transactionId);
		},
		function(e) {
			console.log('Unable to deliver message with transactionId=' + e.data.transactionId + ' Error is: ' + e.error.message);
		});
}
//-----------------------------------------------------------------------------------------------------------------------
//-- Get current location: http://forums.getpebble.com/discussion/21755/pebble-js-location-to-url
var locationOptions = {
	enableHighAccuracy: true, 
	maximumAge: 10000, 
	timeout: 10000
};
//-----------------------------------------------------------------------------------------------------------------------
function locationSuccess(pos) {
	console.log('lat= ' + pos.coords.latitude + ' lon= ' + pos.coords.longitude);
	posLat = (pos.coords.latitude).toFixed(3);
	posLon = (pos.coords.longitude).toFixed(3);
	
	updateWeather();
}
//-----------------------------------------------------------------------------------------------------------------------
function locationError(err) {
	posLat = "0";
	posLon = "0";
	console.log('location error (' + err.code + '): ' + err.message);
}
//-----------------------------------------------------------------------------------------------------------------------
Pebble.addEventListener('appmessage', function(e) {
	console.log("Got message: " + JSON.stringify(e));
	if ('c_cityid' in e.payload) {	//Weather Download
		CityID = e.payload.c_cityid;
		if (CityID === 0)
			navigator.geolocation.getCurrentPosition(locationSuccess, locationError, locationOptions);
		else
			updateWeather();
	}
});
//-----------------------------------------------------------------------------------------------------------------------
function updateWeather() {
	console.log("Updating weather");
	var req = new XMLHttpRequest();
	var URL = "http://api.openweathermap.org/data/2.5/weather?APPID=9a4eed6c813f6d55d0699c148f7b575a&";
	
	if (CityID !== 0)
		URL += "id="+CityID.toString();
	else if (posLat != "0" && posLon != "0")
		URL += "lat=" + posLat + "&lon=" + posLon;
	else
		return; //Error, no position data
	
	URL += "&units=metric&lang="+lang+"&type=accurate";
	console.log("UpdateURL: " + URL);
	req.open("GET", URL, true);
	req.onload = function(e) {
		if (req.readyState == 4) {
			if (req.status == 200) {
				var response = JSON.parse(req.responseText);
				var time = response.dt;
				var temp = Math.round(response.main.temp);//-273.15
				var icon = response.weather[0].icon;
				var cond = response.weather[0].description;
				var name = response.name;
				console.log("Got Weather Data for City: " + name + ", Time: " + time + ", Temp: " + temp + ", Icon:" + icon + "/" + weatherIcon[icon] + ", Cond: " + cond);
				sendMessageToPebble({
					"w_time": time.toString(),
					"w_city": name.toString(),
					"w_temp": temp.toString(),
					"w_icon": weatherIcon[icon],
					"w_cond": cond.toString()
				});
			}
		}
	};
	req.send(null);
}
//-----------------------------------------------------------------------------------------------------------------------
Pebble.addEventListener("showConfiguration", function() {
    var options = JSON.parse(localStorage.getItem('cas_ae_11w_opt'));
    console.log("read options: " + JSON.stringify(options));
    console.log("showing configuration");
	var uri = 'http://panicman.github.io/config_casioae11w.html?title=Casio%20AE-11W%20v1.3';
    if (options !== null) {
        uri +=
			'&colorpbl=' + colorpbl + 
			'&c_inv=' + encodeURIComponent(options.c_inv) + 
			'&c_auto_sw=' + encodeURIComponent(options.c_auto_sw) +
			'&c_vibr=' + encodeURIComponent(options.c_vibr) + 
			'&c_vibr_bt=' + encodeURIComponent(options.c_vibr_bt) + 
			'&c_showsec=' + encodeURIComponent(options.c_showsec) + 
			'&c_datefmt=' + encodeURIComponent(options.c_datefmt) + 
			'&c_dualdiff=' + encodeURIComponent(options.c_dualdiff) + 
			'&c_weather=' + encodeURIComponent(options.c_weather) + 
			'&c_units=' + encodeURIComponent(options.c_units) + 
			'&c_cityid=' + encodeURIComponent(options.c_cityid) + 
			'&c_colsec=' + encodeURIComponent(options.c_colsec) + 
			'&c_colbrd=' + encodeURIComponent(options.c_colbrd);
    }
	console.log("Uri: "+uri);
    Pebble.openURL(uri);
});
//-----------------------------------------------------------------------------------------------------------------------
Pebble.addEventListener("webviewclosed", function(e) {
    console.log("configuration closed");
    if (e.response !== "") {
        var options = JSON.parse(decodeURIComponent(e.response));
        console.log("storing options: " + JSON.stringify(options));
        localStorage.setItem('cas_ae_11w_opt', JSON.stringify(options));
        sendMessageToPebble(options);
    } else {
        console.log("no options received");
    }
});
//-----------------------------------------------------------------------------------------------------------------------