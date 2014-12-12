{  
  'variables': {
      'makefreeimage%': '<!(./deps/makefreeimage.sh)',
   },
  "targets": [
    {
       "target_name": "imgcmp",
       "sources": [ 
            'src/img-cmp512.cc'
       ],
       'conditions': [
		    ['makefreeimage=="true"', {
		      'include_dirs':  [ "../deps/FreeImage/Dist/FreeImage.h" ],
		      "libraries": [ "../deps/FreeImage/Dist/libfreeimage.a"]
		    }, {
		      "libraries": ['-lfreeimage']
		    }]
		],
    }
    
  ]
}

