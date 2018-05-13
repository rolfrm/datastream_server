var loaded_activities = {
};

var activity_checkboxes = {};

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

function cb_checked(evt){
    loaded_activities[evt.target.name] = evt.target.checked;
}

function update_activities(response){
    var activities_div = document.getElementById("activities");
    var items = response.split(',');
    for(var set in items){
	var subitems = items[set].split(':');
	var name = subitems[0];
	if(activity_checkboxes.hasOwnProperty(name) == false){
	    var label = document.createElement('label');
	    var checkbox = document.createElement('input');
	    
	    checkbox.type = "checkbox";
	    checkbox.name = name;
	    checkbox.value = name;
	    checkbox.onchange = cb_checked;
	    label.appendChild(document.createTextNode(name))

	    activity_checkboxes[name] = checkbox;
	    activities_div.appendChild(label);
	    label.appendChild(checkbox);
	    checkbox.checked = subitems[1] == "1";
	    loaded_activities[name] = subitems[1] == "1";
	}
    }
}


function req(url, then, body){
    var httpRequest = new XMLHttpRequest();


    if (!httpRequest) {
        return false;
    }

    httpRequest.open(body == null ? 'GET' : 'POST', url);
    httpRequest.setRequestHeader("Content-type", "text");
    httpRequest.responseType = "text";

    function alertContents() {
        if (httpRequest.readyState === XMLHttpRequest.DONE) {
            if (httpRequest.status === 200) {
		then(httpRequest.responseText);
            }else{
		then("");
	    }
        }
    }
    httpRequest.onreadystatechange = alertContents;
    httpRequest.send(body);
}

function makeRequest() {
    var activities_div = document.getElementById("activities");
    function update_activities2(){
	function next(str){
	    update_activities(str);
	    var body = format_activities();
	    req("/activities", next, body);
	}
	req("/activities", next, format_activities());
    }
    
    function updatetext(txt){
	if(txt === ""){

	}else{

	
	    var areas = document.getElementById("text");
	    var element = document.createTextNode(txt);
	    areas.appendChild(element);
	    var br = document.createElement("br");
	    areas.appendChild(br);
	    //element.textContent += txt;


	}
	var cb = document.getElementById("checkBox");
	if(cb.checked)
	    areas.scrollTo(0,areas.scrollHeight);
	req("/update", updatetext, null);
	}
    req("/update", updatetext, null);
    update_activities2();
}
window.addEventListener("load", makeRequest);
