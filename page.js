var loaded_activities = {
    "Test1" : true,
    "Test2" : true
};

function format_activities(){
    var str = ""
    var first = true;
    for(var item in loaded_activities){
	if(first)
	    first = false;
	else
	    str += ",";
	
	str += item + ":" + (loaded_activities[item] ? "1" : "0");
    }
    return str;
}


function req(url, then, body){
    httpRequest = new XMLHttpRequest();


    if (!httpRequest) {
        return false;
    }
    httpRequest.onreadystatechange = alertContents;
    httpRequest.open('POST', url);
    httpRequest.setRequestHeader("Content-type", "text");
    httpRequest.send(body);

    function alertContents() {
        if (httpRequest.readyState === XMLHttpRequest.DONE) {
            if (httpRequest.status === 200) {
		then(httpRequest.responseText);
            }
        }
    }
}

function makeRequest() {
    var activities_div = document.getElementById("activities");
    function update_activities(){
	function next(str){
	    alert(str);
	    
	    req("/activities", next, format_activities());
	}
	req("/activities", next, format_activities());
    }
    
    function updatetext(txt){
	var cb = document.getElementById("checkBox");
	var areas = document.getElementById("text");
	var element = document.createElement("textarea");
	areas.appendChild(element);
	element.textContent += txt;
	    if(cb.checked)
		areas.scrollTo(0,areas.scrollHeight);
	    
	    req("/update", updatetext, "");
	}
    req("/update", updatetext, "");
    update_activities();
}
window.addEventListener("load", makeRequest);
