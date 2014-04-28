# Run these tests by using the Redis `run-test` command.
#   - First, put this file in the redis `tests/` directory.
#   - Next, edit the `module-add` line in the first test to point to *your*
#     json.so module to load.
#   - Finally, run: ./run-test --single jsonobj

start_server {tags {"jsonobj"}} {
    test {JSONDOC - set simple} {
        r config set module-add /home/matt/repos/krmt/json.so

        r jsondocset menu {
            {"menu": { "header": "SVG Viewer", "items": [ {"id": "Open"}, {"id": "OpenNew", "label": "Open New"}, null, {"id": "ZoomIn", "label": "Zoom In"}, {"id": "ZoomOut", "label": "Zoom Out"}, {"id": "OriginalView", "label": "Original View"}, null, {"id": "Quality"}, {"id": "Pause"}, {"id": "Mute"}, null, {"id": "Find", "label": "Find..."}, {"id": "FindAgain", "label": "Find Again"}, {"id": "Copy"}, {"id": "CopyAgain", "label": "Copy Again"}, {"id": "CopySVG", "label": "Copy SVG"}, {"id": "ViewSVG", "label": "View SVG"}, {"id": "ViewSource", "label": "View Source"}, {"id": "SaveAs", "label": "Save As"}, null, {"id": "Help"}, {"id": "About", "label": "About Adobe CVG Viewer..."} ] }}
        }

    } {1}

    test {JSONDOC - remove simple} {
        r jsondocdel menu
    } {1}

    test {JSONDOC - verify remove cleaned all keys} {
        r keys *
    } {}

    test {JSONDOC - set complex} {
        r jsondocset webapp {
            {"web-app": { "servlet": [   { "servlet-name": "cofaxCDS", "servlet-class": "org.cofax.cds.CDSServlet", "init-param": { "configGlossary:installationAt": "Philadelphia, PA", "configGlossary:adminEmail": "ksm@pobox.com", "configGlossary:poweredBy": "Cofax", "configGlossary:poweredByIcon": "/images/cofax.gif", "configGlossary:staticPath": "/content/static", "templateProcessorClass": "org.cofax.WysiwygTemplate", "templateLoaderClass": "org.cofax.FilesTemplateLoader", "templatePath": "templates", "templateOverridePath": "", "defaultListTemplate": "listTemplate.htm", "defaultFileTemplate": "articleTemplate.htm", "useJSP": false, "jspListTemplate": "listTemplate.jsp", "jspFileTemplate": "articleTemplate.jsp", "cachePackageTagsTrack": 200, "cachePackageTagsStore": 200, "cachePackageTagsRefresh": 60, "cacheTemplatesTrack": 100, "cacheTemplatesStore": 50, "cacheTemplatesRefresh": 15, "cachePagesTrack": 200, "cachePagesStore": 100, "cachePagesRefresh": 10, "cachePagesDirtyRead": 10, "searchEngineListTemplate": "forSearchEnginesList.htm", "searchEngineFileTemplate": "forSearchEngines.htm", "searchEngineRobotsDb": "WEB-INF/robots.db", "useDataStore": true, "dataStoreClass": "org.cofax.SqlDataStore", "redirectionClass": "org.cofax.SqlRedirection", "dataStoreName": "cofax", "dataStoreDriver": "com.microsoft.jdbc.sqlserver.SQLServerDriver", "dataStoreUrl": "jdbc:microsoft:sqlserver://LOCALHOST:1433;DatabaseName=goon", "dataStoreUser": "sa", "dataStorePassword": "dataStoreTestQuery", "dataStoreTestQuery": "SET NOCOUNT ON;select test='test';", "dataStoreLogFile": "/usr/local/tomcat/logs/datastore.log", "dataStoreInitConns": 10, "dataStoreMaxConns": 100, "dataStoreConnUsageLimit": 100, "dataStoreLogLevel": "debug", "maxUrlLength": 500}}, { "servlet-name": "cofaxEmail", "servlet-class": "org.cofax.cds.EmailServlet", "init-param": { "mailHost": "mail1", "mailHostOverride": "mail2"}}, { "servlet-name": "cofaxAdmin", "servlet-class": "org.cofax.cds.AdminServlet"}, { "servlet-name": "fileServlet", "servlet-class": "org.cofax.cds.FileServlet"}, { "servlet-name": "cofaxTools", "servlet-class": "org.cofax.cms.CofaxToolsServlet", "init-param": { "templatePath": "toolstemplates/", "log": 1, "logLocation": "/usr/local/tomcat/logs/CofaxTools.log", "logMaxSize": "", "dataLog": 1, "dataLogLocation": "/usr/local/tomcat/logs/dataLog.log", "dataLogMaxSize": "", "removePageCache": "/content/admin/remove?cache=pages&id=", "removeTemplateCache": "/content/admin/remove?cache=templates&id=", "fileTransferFolder": "/usr/local/tomcat/webapps/content/fileTransferFolder", "lookInContext": 1, "adminGroupID": 4, "betaServer": true}}], "servlet-mapping": { "cofaxCDS": "/", "cofaxEmail": "/cofaxutil/aemail/*", "cofaxAdmin": "/admin/*", "fileServlet": "/static/*", "cofaxTools": "/tools/*"}, "taglib": { "taglib-uri": "cofax.tld", "taglib-location": "/WEB-INF/tlds/cofax.tld"}}}
        }
    } {1}


    test {JSONDOC - remove complex} {
        r jsondocdel webapp
    
    } {1}


    test {JSONDOC - verify remove cleaned all keys} {
        r keys *
    } {}

    test {JSONDOC - add sample types} {
        r jsondocset simple {
            {"a": "b"}
        }

        r jsondocset simplelist {
            {"a": ["b"]}
        }

        r jsondocset simplemap {
            {"a": {"b":"c"}}
        }

        r jsondocset simplemaplist {
            {"a": {"b":["c"]}}
        }

        r jsondocset simplelistmap {
            {"a": [{"b":["c"]}]}
        }

        r jsondocset simplelistmapnumber {
            {"a": [{"b":[3]}]}
        }

        r jsondocset simplenumber {
            {"a": 4}
        }

        r jsondocset simpletrue {
            {"a": true}
        }

        r jsondocset simplefalse {
            {"a": false}
        }

        r jsondocset simplenull {
            {"a": null}
        }
    } {1}

    test {JSONDOC - remove all basic types} {
        r jsondocdel simple simplelist simplemap simplemaplist simplelistmap \
                     simplelistmapnumber simplenumber simpletrue simplefalse \
                     simplenull

    } {10}

    test {JSONDOC - verify remove cleaned all keys} {
        r keys *
    } {}

    test {JSONFIELD - basic get string} {
        r jsondocset simple {
            {"a": "b"}
        }

        r jsonfieldget simple a
    } {"b"}

    test {JSONFIELD - basic get number} {
        r jsondocset simple_number {
            {"a": 300}
        }

        r jsonfieldget simple_number a
    } {300}

    test {JSONFIELD - basic get list (numbers)} {
        r jsondocset simple_list_number {
            {"a": [1, 2, 3, 4]}
        }

        r jsonfieldget simple_list_number a
    } {[1,2,3,4]}

    test {JSONFIELD - basic get list (strings)} {
        r jsondocset simple_list_string {
            {"a": ["a", "b", "c", "d"]}
        }

        r jsonfieldget simple_list_string a
    } {["a","b","c","d"]}

    test {JSONFIELD - basic get list (with map)} {
        r jsondocset simple_list_map {
            {"a": [1, {"d":"e"}, 3, 4]}
        }

        r jsonfieldget simple_list_map a
    } {[1,{"d":"e"},3,4]}

    test {JSONFIELD - basic get map} {
        r jsondocset simple_m {
            {"a": {"b":"c"}}
        }

        r jsonfieldget simple_m a
    } {{"b":"c"}}

    test {JSONFIELD - basic get true} {
        r jsondocset simple_t {
            {"a": true}
        }

        r jsonfieldget simple_t a
    } {true}

    test {JSONFIELD - basic get false} {
        r jsondocset simple_f {
            {"a": false}
        }

        r jsonfieldget simple_f a
    } {false}

    test {JSONFIELD - basic get null} {
        r jsondocset simple_n {
            {"a": null}
        }

        r jsonfieldget simple_n a
    } {null}

    test {JSONDOC - remove all test documents} {
        r jsondocdel simple simple_number simple_list_number simple_list_string simple_list_map simple_m simple_t simple_f simple_n
    } {9}

    test {JSONDOC - verify remove cleaned all keys} {
        r keys *
    } {}

    test {JSONFIELD - multi-layered get list} {
        r jsondocset simple_list_map {
            {"a": [1, {"d":"e"}, 3, 4]}
        }

        r jsonfieldget simple_list_map a
    } {[1,{"d":"e"},3,4]}


    test {JSONFIELD - multi-layered get first} {
        r jsonfieldget simple_list_map a 0
    } {1}

    test {JSONFIELD - multi-layered get second} {
        r jsonfieldget simple_list_map a 1
    } {{"d":"e"}}

    test {JSONFIELD - multi-layered get second key} {
        r jsonfieldget simple_list_map a 1 d
    } {"e"}

    test {JSONFIELD - multi-layered get third} {
        r jsonfieldget simple_list_map a 2
    } {3}

    test {JSONFIELD - multi-layered get fourth} {
        r jsonfieldget simple_list_map a 3
    } {4}

    test {JSONDOC - remove all test documents} {
        r jsondocdel simple_list_map
    } {1}

    test {JSONDOC - verify remove cleaned all keys} {
        r keys *
    } {}

    test {JSONFIELD - deep nested get - level 1} {
        r jsondocset nest {
            {"a": [{"b":[{"c":["d"]}]}]}
        }

        r jsonfieldget nest a
    } {[{"b":[{"c":["d"]}]}]}

    test {JSONFIELD - deep nested get - level 2} {
        r jsonfieldget nest a 0
    } {{"b":[{"c":["d"]}]}}

    test {JSONFIELD - deep nested get - level 3} {
        r jsonfieldget nest a 0 b
    } {[{"c":["d"]}]}

    test {JSONFIELD - deep nested get - level 4} {
        r jsonfieldget nest a 0 b 0
    } {{"c":["d"]}}

    test {JSONFIELD - deep nested get - level 5} {
        r jsonfieldget nest a 0 b 0 c
    } {["d"]}

    test {JSONFIELD - deep nested get - level 6} {
        r jsonfieldget nest a 0 b 0 c 0
    } {"d"}

    test {JSONFIELD - deep nested get - level [too low]} {
        r jsonfieldget nest a 0 b 0 c -47
    } {}

    test {JSONFIELD - deep nested get - level [too high]} {
        r jsonfieldget nest a 0 b 0 c 47
    } {}

    test {JSONDOC - remove all test documents} {
        r jsondocdel nest
    } {1}

    test {JSONDOC - verify remove cleaned all keys} {
        r keys *
    } {}

    test {JSONFIELDDEL - basic delete} {
        r jsondocset basic {
            {"a":"b"}
        }
        r jsonfielddel basic a
    } {1}

    test {JSONFIELDDEL - verify document after delete} {
       r jsondocget basic 
    }

    test {JSONFIELDDEL - basic map delete} {
        r jsondocset basic_map {
            {"a":{"b":"c"}}
        }
        r jsonfielddel basic_map a
    } {1}

    test {JSONFIELDDEL - verify document after delete} {
       r jsondocget basic_map
    } {}

    test {JSONFIELDDEL - basic list delete} {
        r jsondocset basic_list {
            {"a":[3,4]}
        }
        r jsonfielddel basic_list a
    } {1}

    test {JSONFIELDDEL - verify document after delete} {
       r jsondocget basic_list
    } {}

    test {JSONFIELDDEL - basic list delete II} {
        r jsondocset basic_list_ii {
            {"a":["a", "b"]}
        }
        r jsonfielddel basic_list_ii a
    } {1}

    test {JSONFIELDDEL - verify document after delete} {
       r jsondocget basic_list_ii
    } {}

    test {JSONFIELDDEL - nested list delete} {
        r jsondocset basic_list_ii {
            {"a":["a", "b", {"c":"d"}]}
        }
        r jsonfielddel basic_list_ii a
    } {1}

    test {JSONFIELDDEL - verify document after delete} {
       r jsondocget basic_list_ii
    } {}

    test {JSONFIELDDEL - nested list delete direct positional} {
        r jsondocset basic_list_ii_ii {
            {"a":["a", "b", {"c":"d"}]}
        }
        r jsonfielddel basic_list_ii_ii a 1
    } {0}

    test {JSONFIELDDEL - verify document after delete} {
       r jsondocget basic_list_ii_ii
    } {{"a":["a","b",{"c":"d"}]}}

    test {JSONFIELDDEL - nested map to empty map} {
        r jsondocset basic_list_ii_ii_ii {
            {"a":["a", "b", {"c":"d"}]}
        }
        r jsonfielddel basic_list_ii_ii_ii a 2
    } {1}

    test {JSONFIELDDEL - verify document after nested map delete} {
       r jsondocget basic_list_ii_ii_ii
    } {{"a":["a","b",{}]}}

    test {JSONFIELDDEL - nested list to empty list} {
        r jsondocset basic_list_iii {
            {"a":["a", "b", [{"c":"d"}, ["z", "q", {"e":"f"}]]]}
        }
        r jsonfielddel basic_list_iii a 2
    } {1}

    test {JSONFIELDDEL - verify document after nested list delete} {
       r jsondocget basic_list_iii
    } {{"a":["a","b",[]]}}

    test {JSONFIELDDEL - nested map later field delete} {
        r jsondocset basic_map_ii {
            {"a":"b", "c":"d"}
        }
        r jsonfielddel basic_map_ii c
    } {1}

    test {JSONFIELDDEL - verify document after delete} {
       r jsondocget basic_map_ii
    } {{"a":"b"}}

    test {JSONDOC - remove remaining test documents} {
        r jsondocdel basic_map_ii basic_list_ii_ii basic_list_ii_ii_ii basic_list_iii
    } {4}

    test {JSONDOC - verify remove cleaned all keys} {
        r keys *
    } {}

    test {JSONDOCSETBYJSON - set map} {
        r jsondocsetbyjson id {
            {"id":"alpha", "a":"b", "c":"d", "e":"f"}
        }

    } {alpha}

    test {JSONDOCSETBYJSON - set map inside list} {
        r jsondocsetbyjson id {
            [{"id":"beta", "a":"b", "c":"d", "e":"f"}]
        }

    } {beta}

    test {JSONDOCSETBYJSON - set multi-map inside list} {
        r jsondocsetbyjson id {
            [{"id":"gamma", "a":"b", "c":"d", "e":"f"},
             {"a":"b", "id":"delta", "c":"d", "e":"f"},
             {"a":"b", "c":"d", "id":"epsilon", "e":"f"}]
        }

    } {gamma delta epsilon}

    test {JSONDOC - remove remaining test documents} {
        r jsondocdel alpha beta gamma delta epsilon
    } {5}

    test {JSONDOC - verify remove cleaned all keys} {
        r keys *
    } {}

    test {JSONDOCKEYS - get keys of a map} {
        r jsondocset abc {
            {"abc": "def", "hij": "def", "zoo": {"number": 4000}}
        }

        r jsondockeys abc
    } {abc hij zoo}

    test {JSONFIELDINCRBY - increment by integer} {
        r jsonfieldincrby abc zoo number 1000
        r jsonfieldget abc zoo number
    } {5000}

    test {JSONFIELDINCRBYFLOAT - increment by float} {
        r jsonfieldincrbyfloat abc zoo number 14.5
        r jsonfieldget abc zoo number
    } {5014.5}

    test {JSONFIELDRPUSHX - rpush string} {
        r jsondocset def {
            {"a":["abc", "def"]}
        }
        r jsonfieldrpushx def a "\"hij\""
    } {3}

    test {JSONFIELDRPUSHX - rpush string verify} {
        r jsonfieldget def a
    } {["abc","def","hij"]}

    test {JSONFIELDRPUSHX - rpush number} {
        r jsondocset hij {
            {"a":[1, 2, 3, 4, 5]}
        }
        r jsonfieldrpushx hij a 6
    } {6}

    test {JSONFIELDRPUSHX - rpush string verify} {
        r jsonfieldget hij a
    } {[1,2,3,4,5,6]}

    test {JSONFIELDRPOP - rpop number} {
        r jsonfieldrpop hij a
    } {6}

    test {JSONFIELDRPUSHX - rpop string verify} {
        r jsonfieldget hij a
    } {[1,2,3,4,5]}

    test {JSONFIELDRPOP - rpop number} {
        r jsonfieldrpop hij a
    } {5}

    test {JSONFIELDRPUSHX - rpop string verify} {
        r jsonfieldget hij a
    } {[1,2,3,4]}

    test {JSONFIELDRPOP - rpop mixed contents} {
        r jsondocset zed {
            {"a":[1, "two", 3, {"four": null}]}
        }
        r jsonfieldrpop zed a
    } {{"four":null}}

    test {JSONFIELDRPOP - rpop mixed contents verify} {
        r jsonfieldget zed a
    } {[1,"two",3]}

    test {JSONFIELDRPOP - rpop mixed contents} {
        r jsonfieldrpop zed a
    } {3}

    test {JSONFIELDRPOP - rpop mixed contents verify} {
        r jsonfieldget zed a
    } {[1,"two"]}

    test {JSONFIELDRPOP - rpop mixed contents} {
        r jsonfieldrpop zed a
    } {"two"}

    test {JSONFIELDRPOP - rpop mixed contents verify} {
        r jsonfieldget zed a
    } {[1]}

    test {JSONFIELDRPOP - rpop mixed contents last element} {
        r jsonfieldrpop zed a
    } {1}

    test {JSONFIELDRPOP - rpop mixed contents verify empty} {
        r jsonfieldget zed a
    } {[]}

    test {JSONDOC - remove remaining test documents} {
        r jsondocdel abc def hij zed
    } {4}

    test {JSONDOC - verify remove cleaned all keys} {
        r keys *
    } {}
}
