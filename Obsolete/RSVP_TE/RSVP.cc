/*******************************************************************
*
*	This library is free software, you can redistribute it 
*	and/or modify 
*	it under  the terms of the GNU Lesser General Public License 
*	as published by the Free Software Foundation; 
*	either version 2 of the License, or any later version.
*	The library is distributed in the hope that it will be useful, 
*	but WITHOUT ANY WARRANTY; without even the implied warranty of
*	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. 

*	See the GNU Lesser General Public License for more details.
*
*
*********************************************************************/
#include "RSVP.h"
#include "ip_address.h"
#include "MPLSModule.h"

Define_Module( RSVP);

void RSVP::initialize()
{
     LIBTableAccess::initialize();
	//Get router ID
	my_id   = IPAddress(par("local_addr").stringValue()).getInt();
	findRoutingTable();
	findOSPFTE();

	IsIR = par("isIR").boolValue();
	IsER = par("isER").boolValue();


	//Initialize interface address list
	NoOfLinks = rt->totalInterface();
	
}


void RSVP::activity()
{
	cMessage *msg;
	PathMessage *pm;
	ResvMessage *rm;
	PathTearMessage *ptm;
	ResvTearMessage *rtm;
	PathErrorMessage *pem;
	ResvErrorMessage *rem;

	//PathTearMessage *ptm;
	int inInf;
    int i;

	/*********************SETUP RESOURCE******************************/
	//Buid the link table
	int linkNo= 0;
	updateTED();
	std::vector<telinkstate>::iterator ted_iterI;
	telinkstate ted_iter;

    	for (ted_iterI=ted.begin(); ted_iterI != ted.end(); ted_iterI++)
    	{
		ted_iter = (telinkstate)*ted_iterI;
		if(ted_iter.advrouter.getInt() == my_id)
		{
			 LocalAddress[linkNo] = ted_iter.local.getInt();
			 linkNo = linkNo+1;
			
		}
     	}
     	for(i=linkNo+1; i<InLIST_SIZE ;i++)
	{
	LocalAddress[i] =0;
	}
	
	//Debug:
	for(i=0; i< InLIST_SIZE;i++)
	ev << IPAddress(LocalAddress[i]).getString() << "\n";
	
	

	/****************************PROCESS MESSAGES*************************/
	
	while(true)
	{
	  msg = receive();

	 //if (!(strcmp(msg->arrivalGate()->name(), "from_rsvp_app")))
	//	 send(msg, "to_ip");
	 //else
	 {

	  switch (msg->kind())
	  {
	  case PATH_MESSAGE:
          inInf = msg->par("peerInf");
          pm = (PathMessage*)msg;
          PathMsgPro( pm, inInf );
		  break;

	  case RESV_MESSAGE:
		  rm = (ResvMessage*)msg;
		  ResvMsgPro(rm);
		  break;
		
	  case PTEAR_MESSAGE:
		  ptm =(PathTearMessage*)msg;
		  PTearMsgPro(ptm);
		  break;
	  case RTEAR_MESSAGE:
		  rtm = (ResvTearMessage*)msg;
		  RTearMsgPro(rtm);
		  break;
	  case PERROR_MESSAGE:
		  pem =(PathErrorMessage*)msg;
		  PErrorMsgPro(pem);
		  break;
	  case RERROR_MESSAGE:
		  rem =(ResvErrorMessage*)msg;
		  RErrorMsgPro(rem);
		  break;
	  default:
		  ev << "Unrecognized RSVP packet format\n";
		  delete msg;
	  }
	 
	}
	}
}


void RSVP::PathMsgPro(PathMessage *pmsg, int InIf)
{
/*
	if(IsER)
	{
				
	send(pmsg, "to_rsvp_app");
	return;
	}
*/		
		PathStateBlock_t * fPSB = NULL;
		PathStateBlock_t * cPSB = NULL;
		ResvStateBlock_t * activeRSB = NULL;
		int foundFlag= OFF;
		int Resv_Refresh_Needed_Flag = OFF;
		int Path_Refresh_Needed_Flag = OFF;

        std::vector<PathStateBlock_t>::iterator p_iterI;
		PathStateBlock_t					  p_iter;
        std::vector<ResvStateBlock_t>::iterator r_iterI;
		ResvStateBlock_t					  r_iter;
		
		
		ev << "Received PATH MESSAGE\n";
		pmsg->print();

			
	
		EroObj_t* ERO ;
	
		if(pmsg->hasERO())
			ERO = pmsg->getERO();
	

		
		int removeIndex =0;
		if(pmsg->hasERO())
		{
		
			for(int k=0; k < MAX_ROUTE;k++)
			{
			  ev << "ROUTE: " << IPAddress(ERO[k].node).getString() << "\n";
			}
					
			
			for(removeIndex =0; removeIndex < MAX_ROUTE; removeIndex++)
			if(ERO[removeIndex].node ==0 && removeIndex >0)
			{
			ERO[removeIndex-1].node=0;
			break;
			}
		}
	
		
		
		/*
        
   o    Search for a path state block (PSB) whose (session,
        sender_template) pair matches the corresponding objects in the
        message, and whose IncInterface matches InIf.*/
        

        if(!PSBList.empty())
        {
	        for(p_iterI = PSBList.begin(); p_iterI != PSBList.end(); p_iterI++)
		    {
				p_iter = (PathStateBlock_t)*p_iterI;
		    		/*
             1.   If a PSB is found whose session matches the
                  DestAddress and Protocol Id fields of the received
                  SESSION object, but the DstPorts differ and one is
                  zero, then build and send a "Conflicting Dst Port"
                  PERR message, drop the PATH message, and return.*/

			if(p_iter.Session_Object.DestAddress == pmsg->getDestAddress() &&
               p_iter.Session_Object.Protocol_Id == pmsg->getProtId()     &&
               p_iter.Session_Object.DestPort != pmsg->getDestPort ()      && 
              (p_iter.Session_Object.DestPort == 0 || pmsg->getDestPort() == 0 ))
               {
				ev << "Send Path Error Message : Conflicting Destination Port\n";
				return;
               }
               
      	/*
             2.   If a PSB is found with a matching sender host but the
                  Src Ports differ and one of the SrcPorts is zero, then
                  build and send an "Ambiguous Path" PERR message, drop
                  the PATH message, and return.*/


			if(( p_iter.Sender_Template_Object.SrcAddress == pmsg->getSrcAddress() &&
				 p_iter.Sender_Template_Object.SrcPort != pmsg->getSrcPort() )&& 
               ( p_iter.Sender_Template_Object.SrcPort == 0 ||pmsg->getSrcPort()==0 ))                
				{
				ev << "Send Path Error Message : Ambiguous Path\n";
				return;
               }
               
               
               	/*
             3.   If a forwarding PSB is found, i.e., a PSB that matches
                  the (session, sender_template) pair and whose
                  Local_Only flag is off, save a pointer to it in the
                  variable fPSB.  If none is found, set fPSB to NULL.*/

			if( pmsg->isInSession( &p_iter.Session_Object)     &&
				pmsg->equalST( &p_iter.Sender_Template_Object) &&
				p_iter.Local_Only_Flag == OFF )
			   {
				fPSB = &p_iter;
			   }
			else
			  {
				fPSB = NULL;
			  }
			  
		    /** Search for matching of the interface, if PSB found*/
			if( pmsg->isInSession( &p_iter.Session_Object) &&
				pmsg->equalST( &p_iter.Sender_Template_Object) &&
				( p_iter.IncInterface == InIf) )
			    {
		
				ev << "Matching PSB found \n";
				  /*
				  If the PHOP IP address, the LIH, or Sender_Tspec
                  differs between the message and the PSB, copy the new
                  value into the PSB and turn on the Path_Refresh_Needed
                  flag.  If the PHOP IP address or the LIH differ, also
                  turn on the Resv_Refresh_Needed flag.*/
                  
               	if( pmsg->getNHOP() !=  p_iter.Previous_Hop_Address  ||
					pmsg->getLIH()  !=  p_iter.LIH )
					{
						Resv_Refresh_Needed_Flag = ON;
					}               
					
				if( pmsg->getNHOP() !=  p_iter.Previous_Hop_Address  ||
					pmsg->getLIH()  !=  p_iter.LIH                   ||
                  (!pmsg->equalST( (SenderTemplateObj_t*)&p_iter.Sender_Tspec_Object)) )
					{
						p_iter.Previous_Hop_Address =pmsg->getNHOP() ;
					    p_iter.LIH =pmsg->getLIH();
					 				
	                    //Copy the Sender_Tspec_Object
                        p_iter.Sender_Tspec_Object.link_delay = pmsg->getDelay();
                        p_iter.Sender_Tspec_Object.req_bandwidth=pmsg->getBW();
						 
						Path_Refresh_Needed_Flag = ON;
					}
			
					cPSB = &p_iter;	//Remember the pointer
					foundFlag = ON;
					break;
			
			}                 
          }
      }
      
       if( foundFlag == OFF )
		{                                          /*
			o    If there was no matching PSB, then:

             1.   Create a new PSB.*/

			ev << "No matching PSB \n";
	
			PathStateBlock_t* psbEle =  new PathStateBlock_t;
			psbEle->Session_Object.DestAddress = pmsg->getDestAddress();
			psbEle->Session_Object.DestPort    = pmsg->getDestPort();
			psbEle->Session_Object.Protocol_Id = pmsg->getProtId();
			psbEle->Session_Object.setupPri = pmsg->getSetupPri();
			psbEle->Session_Object.holdingPri = pmsg->getHoldingPri();
			psbEle->Session_Object.Tunnel_Id =   pmsg->getTunnelId();
			psbEle->Session_Object.Extended_Tunnel_Id=pmsg->getExTunnelId();
			
			
		    psbEle->Sender_Template_Object.SrcAddress = pmsg->getSrcAddress();
		    psbEle->Sender_Template_Object.SrcPort    = pmsg->getSrcPort();
		    psbEle->Sender_Template_Object.Lsp_Id = pmsg->getLspId();
		    
			psbEle->Sender_Tspec_Object.link_delay    = pmsg->getDelay();
			psbEle->Sender_Tspec_Object.req_bandwidth = pmsg->getBW();
			
			
			psbEle->Previous_Hop_Address = pmsg->getNHOP();
			psbEle->LIH = pmsg->getLIH();
			psbEle->LabelRequest = *(pmsg->getLabelRequest());
			psbEle->SAllocated =false;
	
	
			if(IsER)
			{
				psbEle->OutInterface_List =-1;
				PSBList.push_back(*psbEle);
				cPSB = psbEle;//PSBList.end();
				ev << "Added new PSB:\n";
				printPSB(psbEle);
						
				send(pmsg, "to_rsvp_app");
				return;
			}
			
			if( IsLocalAddress(pmsg->getSrcAddress() ) )
			{
				ev << "Local Application\n";
				//psbEle->OutInterface_List = pmsg->getSrcAddress();
				psbEle->IncInterface = 0 ; //UNDEFINED IP ADDRESS
				//LocalInFlag = ON;

				psbEle->Local_Only_Flag = ON;
			}
			else
			{
				ev << "Not Local Application\n";
				psbEle->IncInterface = InIf ;

				psbEle->Local_Only_Flag = OFF;
			}

   			
			//If next hop is strict hop or removeIndex >1
			if((removeIndex >1) && (!ERO[removeIndex-1].L))
			Mcast_Route_Query(pmsg->getSrcAddress() , InIf,  ERO[removeIndex-2].node,
                               &psbEle->OutInterface_List ); //Find the outgoing Inf list
   			else
			Mcast_Route_Query( pmsg->getSrcAddress(), InIf, pmsg->getDestAddress(),
                               &psbEle->OutInterface_List ); //Find the outgoing Inf list
   			
				
			if(doCACCheck(pmsg, psbEle->OutInterface_List))
			{
				PSBList.push_back(*psbEle);
				cPSB = psbEle;//PSBList.end();
				ev << "Added new PSB:\n";
				printPSB(psbEle);
				Path_Refresh_Needed_Flag = ON;
			}
			
			
			

		}
		
		if( cPSB == NULL )
		{
			return;
		}
		
		if( Path_Refresh_Needed_Flag == OFF )
		{
			ev<<"No Path Refresh Needed : Leaving PathMsgPro\n";
			return;
		}
		
		else
		{
			int tOutList[InLIST_SIZE];
			int olFlag = OFF;
	
			Mcast_Route_Query( 
				cPSB->Sender_Template_Object.SrcAddress, InIf,
				cPSB->Session_Object.DestAddress, tOutList );/*
			
			 2.   If OutInterface_list is not empty, execute the PATH
                  REFRESH event sequence (below) for the sender defined
                  by the PSB.*/
			if( cPSB ) 
			{
				ev<<"Execute PATH REFRESH SEQUENCE\n";
				
			if((pmsg->hasERO()) )
				PathRefresh( cPSB , cPSB->OutInterface_List, ERO );
			else
				PathRefresh( cPSB , cPSB->OutInterface_List, NULL );

			
					
				
			}/*
			
             3.   Search for any matching reservation state, i.e., an
                  RSB whose Filter_spec_list includes a FILTER_SPEC
                  matching the SENDER_TEMPLATE and whose OI appears in
                  the OutInterface_list, and make this the `active RSB'.
                  If none is found, drop the PATH message and return.*/

			activeRSB = NULL; //At most one
			for(r_iterI= RSBList.begin();r_iterI != RSBList.end(); r_iterI++)
			{
				r_iter = (ResvStateBlock_t)*r_iterI;
				
				int index =0;
				for(index =0;index< InLIST_SIZE;index++)
				if(r_iter.Filter_Spec_Object[index].SrcAddress == cPSB->Sender_Template_Object.SrcAddress &&
				   r_iter.Filter_Spec_Object[index].SrcPort    == cPSB->Sender_Template_Object.SrcPort)
				   break;
				
				if(index == InLIST_SIZE)
				continue;
				
				
					
				if(r_iter.OI == cPSB->OutInterface_List )
				{
				activeRSB = &r_iter;
							
				}
					
				
				
			}
			
			if( activeRSB )
			{
				UpdateTrafficControl(activeRSB);
			}
			else
			{
				ev << "No active RSB found\n";
			}
			ev << "Leaving PathMsgPro\n";
			return;
			
		}
		
		
}



void
RSVP::ResvMsgPro(ResvMessage *rmsg)
{
	/*
	if(IsER)
	{
	send(rmsg, "to_ip");
	return;
	}
	*/
	
	
	ResvStateBlock_t * activeRSB;
	std::vector<PathStateBlock_t> locList;
    std::vector<PathStateBlock_t>::iterator p_iterI;
	PathStateBlock_t		p_iter;
    std::vector<ResvStateBlock_t>::iterator r_iterI;
	ResvStateBlock_t r_iter;
	int ResvStyle = rmsg->getStyle();
	int OI = rmsg->getLIH();
	int count =0;



	int* Refresh_PHOP_list = new int[InLIST_SIZE];
	
	int Resv_Refresh_Needed_Flag = OFF;

    int NeworMod = OFF;
	int psbFoundFlag = OFF;
	int inx;
	FlowDescriptor_t *fdlist = NULL;
	FilterSpecObj_t *Filtss=NULL;


	ev << "Entered ResvMsgPro ";
	ev << "Process RESV message with content\n";
	rmsg->print();



	for(int i=0; i<InLIST_SIZE; i++)
	{
	Refresh_PHOP_list[i] = 0;
	}
	/*
	
     o    Check the path state

             1.   If there are no existing PSB's for SESSION then build
                  and send a RERR message (as described later)
                  specifying "No path information", drop the RESV
                  message, and return.*/
	
	if(PSBList.empty())
	{
	ev << " Error - No session PSB found\n";
	return;
	
	}       /*
	        2.   If a PSB is found with a matching sender host but the
                  SrcPorts differ and one of the SrcPorts is zero, then
                  build and send an "Ambiguous Path" PERR message, drop
                  the RESV message, and return.*/
 	ev << "****************INFO of Available PSBs in this router**********\n";
 	
	for(p_iterI = PSBList.begin(); p_iterI != PSBList.end(); p_iterI++)
	{
		p_iter = (PathStateBlock_t)*p_iterI;
		printPSB(&p_iter);

		for(inx=0;inx<InLIST_SIZE;++inx)
		{
			fdlist = rmsg->getFlowDescriptorList() + inx;

			//Check valid FilterSpec only
			if(fdlist->Filter_Spec_Object.SrcAddress==0)
				continue;
			
			if((p_iter.Sender_Template_Object.SrcAddress == fdlist->Filter_Spec_Object.SrcAddress)  &&
			   (p_iter.Sender_Template_Object.SrcPort    != fdlist->Filter_Spec_Object.SrcPort   )  && 
			   (fdlist->Filter_Spec_Object.SrcPort == 0)  || 
			   (p_iter.Sender_Template_Object.SrcPort == 0 ) )
			{
			
				ev << "Ambiguous Path \n";
				return;
			}
		}
		
		
		if( rmsg->isInSession(&p_iter.Session_Object ))
		{
			psbFoundFlag = ON;
		}
	}
	if( psbFoundFlag == OFF )
	{
		ev << "No Path Infomation\n";
		return;
	} 
		
	
	/* o    Check for incompatible styles.

             If any existing RSB for the session has a style that is
             incompatible with the style of the message, build and send
             a RERR message specifying "Conflicting Style", drop the
             RESV message, and return.*/
/*
    for(int m=0;m< RSBList.size();m++)
    	{
		if(rmsg->isInSession(&(RSBList[m].Session_Object)))
			if(ResvStyle != r_iter.style)
			{
				ev << "RSVP RSB Error: Mismatch of style detected\n";
				return;
			}
	}
  */
	
	
	/*
	Process the flow descriptor list to make reservations, as
    follows, depending upon the style.  The following uses a filter
    spec list struct Filtss of type FILTER_SPEC* (defined earlier).

    For FF style: execute the following steps independently for each
    flow descriptor in the message, i.e., for each (FLOWSPEC,
    Filtss) pair.  Here the structure Filtss consists of the
    FILTER_SPEC from the flow descriptor.

    For SE style, execute the following steps once for (FLOWSPEC,
    Filtss), with Filtss consisting of the list of FILTER_SPEC
    objects from the flow descriptor.*/

		
	int totalLoop =0;
	if(ResvStyle==FF_STYLE)
	{
		totalLoop = InLIST_SIZE;
	}
	else
	{
		totalLoop =1;
		Filtss = new FilterSpecObj_t[InLIST_SIZE];
		for(inx=0;inx<InLIST_SIZE;++inx)
		{
			fdlist = rmsg->getFlowDescriptorList() + inx;

			//copy all FilterSpec to Filtss
			(*(Filtss + inx)).Lsp_Id = fdlist->Filter_Spec_Object.Lsp_Id;
			(*(Filtss + inx)).SrcAddress=fdlist->Filter_Spec_Object.SrcAddress;
			(*(Filtss + inx)).SrcPort = fdlist->Filter_Spec_Object.SrcPort;

		}

	}

	for(inx=0;inx<totalLoop;++inx)
	{
		fdlist = rmsg->getFlowDescriptorList() + inx;
	
		ev << "Process FilterSpec in ResvMessage with style = " << ResvStyle << "\n";

		//If the style is FF, we need to process each FilterSpec
		if(ResvStyle ==FF_STYLE)
		{
			Filtss = &fdlist->Filter_Spec_Object;
			printSenderTemplateObject((SenderTemplateObj_t*)(Filtss));
		}
		else
		{
			for(int m =0; m< InLIST_SIZE;m++)
			{
				printSenderTemplateObject((SenderTemplateObj_t*)(Filtss+m));

			}
		}


		//Process valid FilterSpec only
		if(fdlist->Filter_Spec_Object.SrcAddress ==0)
			 		continue;
		
	
		for(p_iterI = PSBList.begin(); p_iterI != PSBList.end(); p_iterI++)
		{
			p_iter = (PathStateBlock_t)*p_iterI;
			
			//Should I make the code as stupid as possible
			if(ResvStyle == FF_STYLE)
			{

				if( p_iter.Sender_Template_Object.SrcAddress == (*Filtss).SrcAddress &&
					p_iter.Sender_Template_Object.SrcPort == (*Filtss).SrcPort )
           			{
				
						if(  p_iter.OutInterface_List== OI )
						{
							locList.push_back(p_iter);
							break; //Expect one PSB match only
						}			
    	        
					}
			}
			else
			{
				for(int m =0; m< InLIST_SIZE;m++)
				{
					if( p_iter.Sender_Template_Object.SrcAddress == (*(Filtss+m)).SrcAddress &&
					p_iter.Sender_Template_Object.SrcPort == (*(Filtss+m)).SrcPort )
           			{
				
						if(  p_iter.OutInterface_List== OI )
						{
							locList.push_back(p_iter);
							break; //Expect one SenderTemplate match only
						}			
    	        
					}

				}
			}

	
		}
		
		
		if( locList.empty() )
		{
            		ev << "No sender information\n";
	        	return;
		}                                /*
		 2. Ignore (valid check)
		 3. Ignore (valid check)
         4.   Add the PHOP from the PSB to Refresh_PHOP_list, if the
         PHOP is not already on the list.*/
		
        for(p_iterI = locList.begin(); p_iterI != locList.end(); p_iterI++)
		{
			p_iter =(PathStateBlock_t)*p_iterI;
			int flag = OFF;
			
			for(int i=0; i<InLIST_SIZE; ++i) 
			{
				if(Refresh_PHOP_list[i] == p_iter.Previous_Hop_Address )
				{
					flag=ON;
					break;
				}
			}		
			if( flag == OFF )
			{
					Refresh_PHOP_list[count] = p_iter.Previous_Hop_Address;
					count = count+1;
	
			}

		}
	                                 /*

		
	    o    Find or create a reservation state block (RSB) for
             (SESSION, NHOP).  If the style is distinct, Filtss is also
             used in the selection.  Call this the "active RSB".*/
			

		activeRSB = NULL;
	
		for(r_iterI = RSBList.begin(); r_iterI != RSBList.end(); r_iterI++)
		{
			r_iter = (ResvStateBlock_t)*r_iterI;
			bool matchRSB =false;
			if(!(rmsg->isInSession(&r_iter.Session_Object)))
				continue;
			if(r_iter.Next_Hop_Address != rmsg->getNHOP())
				continue;
	
		 activeRSB= &r_iter;
		 break;
		  
		}

		if( activeRSB == NULL )
		{
			

			Resv_Refresh_Needed_Flag = ON;
			/** Create a new RSB **/
			ev << "Create a new RSB\n";
			activeRSB = new ResvStateBlock_t ;
			activeRSB->Session_Object.DestAddress = rmsg->getDestAddress();
			activeRSB->Session_Object.DestPort    = rmsg->getDestPort();
			activeRSB->Session_Object.Protocol_Id = rmsg->getProtId();
			activeRSB->Session_Object.setupPri = rmsg->getSetupPri();
			activeRSB->Session_Object.holdingPri= rmsg->getHoldingPri();
			activeRSB->Session_Object.Tunnel_Id= rmsg->getTunnelId();
			activeRSB->Session_Object.Extended_Tunnel_Id=rmsg->getExTunnelId();
			
			
			activeRSB->Next_Hop_Address = rmsg->getNHOP();
			activeRSB->OI = OI;
			activeRSB->style = rmsg->getStyle();
			
			for(int i=0; i< InLIST_SIZE;i++)
			{   
				fdlist = rmsg->getFlowDescriptorList() +i;
				activeRSB->Filter_Spec_Object[i].SrcAddress = fdlist->Filter_Spec_Object.SrcAddress;
				activeRSB->Filter_Spec_Object[i].SrcPort    = fdlist->Filter_Spec_Object.SrcPort;
				activeRSB->Filter_Spec_Object[i].Lsp_Id = fdlist->Filter_Spec_Object.Lsp_Id;
				
				//Record routes
			
				for(int c=0;c< MAX_ROUTE;c++)
				activeRSB->RRO[i][c] = fdlist->RRO[c];
			
				activeRSB->label[i] =-1; //Default 
            }
			activeRSB->Flowspec_Object.link_delay    = fdlist->Flowspec_Object.link_delay;
			activeRSB->Flowspec_Object.req_bandwidth = fdlist->Flowspec_Object.req_bandwidth;

	
			NeworMod = ON;
			RSBList.push_back(*activeRSB);

			ev << "Adding new RSB\n";
			printRSB(activeRSB);
			
			//for(int i =0; i< InLIST_SIZE;i++)
			//if((rmsg->getFlowDescriptorList())[i].Filter_Spec_Object.SrcAddress == my_id)
			if(IsIR)
			{
				//cMessage* msg = new cMessage();
				//msg->addPar("receiverAck") =   rmsg->getDestAddress();
				send(rmsg, "to_rsvp_app");
				return;
			}
			
			//activeRSB = RSBList.end();
		}
		else
		{ 
		/*
	    o    If the active RSB is not new, check whether Filtss from the
             message contains FILTER_SPECs that are not in the RSB; if
             so, add the new FILTER_SPECs and turn on the NeworMod flag.*/

					
		 fdlist = rmsg->getFlowDescriptorList();
		
		 for(int k=0;k<InLIST_SIZE;k++)
		  {
		  	fdlist = rmsg->getFlowDescriptorList()+k;
			if((r_iter.Filter_Spec_Object[k].SrcAddress != fdlist->Filter_Spec_Object.SrcAddress) ||
			    (r_iter.Filter_Spec_Object[k].SrcPort    != fdlist->Filter_Spec_Object.SrcPort) ||
				r_iter.Filter_Spec_Object[k].Lsp_Id != fdlist->Filter_Spec_Object.Lsp_Id)
			{
				r_iter.Filter_Spec_Object[k].SrcAddress = fdlist->Filter_Spec_Object.SrcAddress;
				r_iter.Filter_Spec_Object[k].SrcPort    = fdlist->Filter_Spec_Object.SrcPort;
				r_iter.Filter_Spec_Object[k].Lsp_Id = fdlist->Filter_Spec_Object.Lsp_Id;

				ev << "RSB found and has been modified\n";

				NeworMod = ON;
			}
		  }
		
		  /*
			
			o    If the active RSB is not new, check whether STYLE, FLOWSPEC
            or SCOPE objects have changed; if so, copy changed object
            into RSB and turn on the NeworMod flag.*/
            fdlist = rmsg->getFlowDescriptorList();
			if(!(activeRSB->Flowspec_Object.link_delay == fdlist->Flowspec_Object.link_delay &&
			     activeRSB->Flowspec_Object.req_bandwidth == fdlist->Flowspec_Object.req_bandwidth))
			{
				activeRSB->Flowspec_Object.link_delay = fdlist->Flowspec_Object.link_delay;
				activeRSB->Flowspec_Object.req_bandwidth=fdlist->Flowspec_Object.req_bandwidth;
				NeworMod = ON;
			}

		}


		if( NeworMod == OFF )
			return;

		//ER does not need to update the traffic control as no outlink concerned
		//if(!IsER)
		{
			if(UpdateTrafficControl(activeRSB)==-1)
			{
				ev << "Update traffic control fails\n";
				Resv_Refresh_Needed_Flag = OFF;
				ResvErrorMessage* errorMsg = new ResvErrorMessage();
				errorMsg->setSession(&activeRSB->Session_Object);
				errorMsg->addPar("dest_addr") = 
				IPAddress(activeRSB->Session_Object.DestAddress).getString();
				errorMsg->addPar("src_addr") =IPAddress(my_id).getString();
				send(errorMsg, "to_ip");

			}
			else
				Resv_Refresh_Needed_Flag = ON;
				
		}
		//else
		//{
		//	Resv_Refresh_Needed_Flag = ON;
		//}
	}

	if(IsIR)
	{
			//cMessage* msg = new cMessage();
			//msg->addPar("receiverAck") =   rmsg->getDestAddress();
			send(rmsg, "to_rsvp_app");
			return;
	}

	if( Resv_Refresh_Needed_Flag == ON )
	{
	
		ev<< "Sending RESV REFRESH for each PHOP in Refresh_PHOP_list\n";
		for(int i=0; i<InLIST_SIZE; ++i)  
		{
			if(Refresh_PHOP_list[i] !=0 )
			{
				/******************
				Install new label
				******************/
				
				
				
			
				for(int k=0;k<InLIST_SIZE;k++)
				{
					 fdlist = rmsg->getFlowDescriptorList()+k;
					if(!IsER)
					{
						if((*fdlist).Filter_Spec_Object.SrcAddress !=0)
						{
						
							int outInf = rmsg->getLIH();
							int outInfIndex = rt->interfaceAddressToNoNew(IPAddress(outInf));
							int inInf=0;
							getIncInet(Refresh_PHOP_list[i], &inInf);
						
							int inInfIndex = rt->interfaceAddressToNoNew(IPAddress(inInf));
							int label = (*fdlist).label;

							if(label !=-1)
							{
								int lsp_id = (*fdlist).Filter_Spec_Object.Lsp_Id;

								char* outInfName = (rt->getInterfaceByIndex(outInfIndex))->name;
								char* inInfName =  (rt->getInterfaceByIndex(inInfIndex))->name;
							
								int inLabel =-2;


                if(IsIR)
									inLabel =lt->installNewLabel(label, string(inInfName), string(outInfName),lsp_id, PUSH_OPER);
								else if(IsER)
									inLabel =lt->installNewLabel(label, string(inInfName), string(outInfName),lsp_id, POP_OPER);
								else
									inLabel =lt->installNewLabel(label, string(inInfName), string(outInfName),lsp_id, SWAP_OPER);
							

								ev << "INSTALL new label:\n";
								ev << "(inInf, outInf, inL, outL,fec) =("<< inInfName << "," << outInfName << "," <<
								inLabel << "," << label << "," << lsp_id <<"\n";
								
								activeRSB->label[k]= inLabel;
							}
							else activeRSB->label[k] =-1;
							
						
						}
						else
						activeRSB->label[k] =-1;
					}
					else 
						activeRSB->label[k]= (*fdlist).label;
				
				}
				
				ResvRefresh( activeRSB, Refresh_PHOP_list[i] );
			}
		}
	}
	
	for(p_iterI = locList.begin(); p_iterI != locList.end(); p_iterI++)
	{
			p_iter =(PathStateBlock_t)*p_iterI;
			p_iter.SAllocated =true;
	
	}
	return;




}

void RSVP::PTearMsgPro(PathTearMessage* pmsg)
{
	ev << "********Enter Path Tear Message Pro******************\n";

	PathStateBlock_t* fPSB=NULL;
	int n,m;


	/*
        o    Search for a PSB whose (Session, Sender_Template) pair
             matches the corresponding objects in the message.  If no
             matching PSB is found, drop the PTEAR message and return.*/

	
   if(!PSBList.empty())
   {
	  for(m=0; m < PSBList.size();m++)
	  {

		if( pmsg->isInSession( &PSBList[m].Session_Object)     &&
			pmsg->equalST( &PSBList[m].Sender_Template_Object) )
		{
				fPSB = &PSBList[m];
			
			  /* o    Forward a copy of the PTEAR message to each outgoing
					 interface listed in OutInterface_list of the PSB.*/

			   PathTearMessage* ptm = new PathTearMessage;
			   ptm->setContent(pmsg);
			   
			   if(!IsER)
			   {
				   int peerIP =0;
				   getPeerIPAddress(fPSB->OutInterface_List, &peerIP);
				   ptm->addPar("dest_addr") = IPAddress(peerIP).getString();
				   ptm->addPar("src_addr") = IPAddress(my_id).getString();

				   ev << "Sending PATH TEAR MESSAGE to " << IPAddress(peerIP).getString();
				   send(ptm, "to_ip");
			   }

	

     /*    o  Find each RSB that matches this PSB, i.e., whose
             Filter_spec_list matches Sender_Template in the PSB and
             whose OI is included in OutInterface_list.*/
			 ResvStateBlock_t* mRSB=NULL;

			
			 for(n=0; n< RSBList.size();n++)
			{
				
				int l =0;
				for(l=0;l< InLIST_SIZE;l++)
				if(RSBList[n].Filter_Spec_Object[l].SrcAddress == fPSB->Sender_Template_Object.SrcAddress &&
						RSBList[n].Filter_Spec_Object[l].SrcPort    == fPSB->Sender_Template_Object.SrcPort)
				break;
				
				if(l == InLIST_SIZE)
				continue;
				
						
				if(RSBList[n].OI == fPSB->OutInterface_List )
					{
						//Remove the filter
					ev << "Remove the matching filter in RSB\n";
						RSBList[n].Filter_Spec_Object[l].SrcAddress =0;
						RSBList[n].Filter_Spec_Object[l].SrcPort =0;
						RSBList[n].Filter_Spec_Object[l].Lsp_Id =-1;
					


						
						mRSB = &RSBList[n];
						

						UpdateTrafficControl(mRSB);

					
						break;
									
					}
							
				
						
			}
			int count=0;
			for(count=0;count< InLIST_SIZE;count++)
				if(RSBList[n].Filter_Spec_Object[count].SrcAddress!=0)
					break;

						
			if(count==InLIST_SIZE)
			{
				RemoveTrafficControl(mRSB);
				//RSBList.erase(r_iterI);
				RSBList[n].Session_Object.DestAddress=0;
				RSBList[n].Session_Object.DestPort =0;

			}
		
		
			break;
		
		
	
		} //end compare
	  
	  
	  }//End for
	  }//End if
	  if(fPSB==NULL)
	  {
		  ev << "No matching PSB found\n";
		  
	  }
	  else
	  {
		  ev << "Delete PSB\n";
		  printPSB(fPSB);
		  PSBList[n].Session_Object.DestAddress=0;
		  PSBList[n].Session_Object.DestPort =0;
	  }

	  if(!IsER)
	  delete pmsg;
	  else
		  send(pmsg, "to_rsvp_app");



}

void RSVP::RTearMsgPro(ResvTearMessage* rmsg)
{

	ev << "***************************ENTER RERSV TEAR MSG PRO*****************************\n";
	
	ResvStateBlock_t * activeRSB;
	std::vector<PathStateBlock_t> locList;
    
	int nexthop;
	FlowDescriptor_t *fdlist = NULL;
	int n, m;

	
	bool found =false;


	ev << "Entered Resv Tear Message Pro ";
	ev << "Process ResvTear message with content\n";
	rmsg->print();
	int OI = rmsg->getLIH();



	for(int inx=0;inx<InLIST_SIZE;++inx)
	{
		fdlist = rmsg->getFlowDescriptorList() + inx;
		if(fdlist->Filter_Spec_Object.SrcAddress ==0 ||
			fdlist->Filter_Spec_Object.SrcPort==0)
			 		continue;
		
		printSenderTemplateObject((SenderTemplateObj_t*)(&fdlist->Filter_Spec_Object));
		//printSenderTspecObject((SenderTspecObj_t*) );
		
		for(m=0; m< PSBList.size();m++)
		{
			if( PSBList[m].Sender_Template_Object.SrcAddress == fdlist->Filter_Spec_Object.SrcAddress &&
				PSBList[m].Sender_Template_Object.SrcPort == fdlist->Filter_Spec_Object.SrcPort )
           		{
				
					if(  PSBList[m].OutInterface_List== OI ) 
					{
						found =true;
						nexthop = PSBList[m].Previous_Hop_Address ;
						break;
					}
					   
					
    	        
			}
	
		}
		if(found)
		{
			
			//PSBList.erase(PSBList[m]I);
			PSBList[m].Session_Object.DestAddress=0;
			PSBList[m].Session_Object.DestPort =0;
			break;
		}
	}

              

		activeRSB = NULL;
		
		
		for(n=0; n< RSBList.size();n++)
		{
		
			if(  !(rmsg->isInSession(&RSBList[n].Session_Object)))
			continue;
			if(RSBList[n].Next_Hop_Address != rmsg->getNHOP())
			continue;
			
		
			int k=0;
			for(k =0;k< InLIST_SIZE;k++)
			{
			fdlist = rmsg->getFlowDescriptorList()+k;
			if((RSBList[n].Filter_Spec_Object[k].SrcAddress != fdlist->Filter_Spec_Object.SrcAddress) ||
			    (RSBList[n].Filter_Spec_Object[k].SrcPort    != fdlist->Filter_Spec_Object.SrcPort))
				{
				break;
				}
			}
			if(k != InLIST_SIZE)
			{
			activeRSB = &RSBList[n];
			break;
			}
		}

		if( activeRSB == NULL )
		{
			ev << "Cannot find the matching RSB\n";
			return;
		}
		else
		{
			if(!IsIR)
			{
			ev<< "Sending RESV TEAR for each PHOP in Refresh_PHOP_list\n";
			RTearFwd(activeRSB, nexthop);
			delete rmsg;
			}
			else
				send(rmsg, "to_rsvp_app");

			activeRSB->Flowspec_Object.req_bandwidth =0;
			UpdateTrafficControl(activeRSB);
			//RemoveTrafficControl(activeRSB);
			RSBList[n].Session_Object.DestAddress=0;
			RSBList[n].Session_Object.DestPort =0;
			
		}

			

}


void
RSVP::PErrorMsgPro(PathErrorMessage* pmsg)
{
	
ev << "************************ENTER PATH ERROR MESSAGE PRO***************************\n";	
	if(IsIR)
	{
		send(pmsg, "to_rsvp_app");
		return;
	}

	std::vector<PathStateBlock_t>::iterator p_iterI;
	PathStateBlock_t		p_iter;


	 for(p_iterI = PSBList.begin(); p_iterI != PSBList.end(); p_iterI++)
	 {
		 p_iter = (PathStateBlock_t)*p_iterI;
		 if(!(pmsg->isInSession(&p_iter.Session_Object)))
			 continue;
		 if(pmsg->getSrcAddress() == p_iter.Sender_Template_Object.SrcAddress &&
			 pmsg->getSrcPort() == p_iter.Sender_Template_Object.SrcPort)
		 break;

	 }
	 if(p_iterI == PSBList.end())
	 {
		 ev << "Error cannot find path for PathErrorMessage\n";
		 delete pmsg;
		 return;
	 }
	 pmsg->addPar("src_addr") = IPAddress(my_id).getString();
	 pmsg->addPar("dest_addr") = IPAddress(p_iter.Previous_Hop_Address).getString();
	 ev << "Propagate PATH_ERROR back to " << IPAddress(p_iter.Previous_Hop_Address).getString() <<"\n";
	 send(pmsg, "to_ip");


}

void
RSVP::RErrorMsgPro(ResvErrorMessage *rmsg)
{
	//Simple way 
	send(rmsg, "to_ip");


}
void 
RSVP::PathRefresh( PathStateBlock_t *psbEle , int OI, EroObj_t* ero )
{
	PathMessage* pm = new PathMessage;
	
	int data_len;     /*


    o    Insert TIME_VALUES object into the PATH message being
         built.  Compute the IP TTL for the PATH message as one less
         than the TTL value received in the message.  However, if
         the result is zero, return without sending the PATH
         message.

    o    Create a sender descriptor containing the SENDER_TEMPLATE,
         SENDER_TSPEC, and POLICY_DATA objects, if present in the
         PSB, and pack it into the PATH message being built.*/
	
	//Get the destination address
	int dest = psbEle->Session_Object.DestAddress;
	
	pm->setSession(&psbEle->Session_Object);
	pm->setLabelRequest(&psbEle->LabelRequest);
	pm->setRTime(5);

	pm->setSenderTemplate(&psbEle->Sender_Template_Object);
	pm->setSenderTspec(&psbEle->Sender_Tspec_Object); /*
	
	  
	o    Send a copy of the PATH message to each interface OI in
         OutInterface_list.  Before sending each copy:

             1.   Ignore E_Police flag 

             2.   Ignore ADSPEC object

             3.   Insert into its PHOP object the interface address and
                  the LIH for the interface.*/

	
	RsvpHopObj_t* hop = new RsvpHopObj_t;
	hop->Logical_Interface_Handle = OI;
	hop->Next_Hop_Address = my_id;

	pm->setHop(hop);

	if(ero!=NULL)
	{
		pm->setERO(ero);
		pm->addERO(true);
	}
	else
		pm->addERO(false);

	//ev << "CHECK ERO\n";
	//for(int j=0;j< MAX_ROUTE;j++)
	//ev << IPAddress(ero[j]).getString() << "\n";
	

	//pm->setLength(1); //Other small value so no fragmentation, 1-dummy value

	pm->addPar("src_addr") = IPAddress(my_id).getString();
	
	int finalAddr = pm->getDestAddress();
	ev << "Final address " << IPAddress(finalAddr).getString() << "\n";
	int nextPeerIP =0;
	int nextPeerInf=0;
	int index =0;

	if(ero!=NULL)
	{
		for(index =0; index< MAX_ROUTE;index++)
		if((*(ero+index)).node==0)
		break;
		
		if(index >0)
		nextPeerIP = (*(ero+index-1)).node;
		else
		{
			ev << "Fail to locate resource\n";
			return;
		}
		if((*(ero+index-1)).L ==false)
		getPeerInet(nextPeerIP, &nextPeerInf);
	}
	
	//If cannot base on ERO to find out next hop
	if(nextPeerIP ==0)
	{
		Mcast_Route_Query(0, 0, finalAddr,&nextPeerInf );
		getPeerIPAddress(nextPeerInf, &nextPeerIP);
	}


	pm->addPar("dest_addr")=IPAddress(nextPeerIP).getString();
	pm->addPar("peerInf") = nextPeerInf;
	ev << "Next peer IP " << IPAddress(nextPeerIP).getString() << "\n";

	send(pm, "to_ip");



}



void
RSVP::ResvRefresh( ResvStateBlock_t *rsbEle, int PH )
{

	int i;
	
	int PHOP;
	int found =OFF;
	int B_Merge=OFF;
	ResvMessage* outRM = new ResvMessage;
    FlowSpecObj_t* Tc_Flowspec = new FlowSpecObj_t;
    std::vector<PathStateBlock_t>::iterator p_iterI;
	PathStateBlock_t p_iter;
    std::vector<ResvStateBlock_t>::iterator r_iterI;
	ResvStateBlock_t r_iter;
    std::vector<TrafficControlStateBlock_t>::iterator t_iterI;
	TrafficControlStateBlock_t t_iter;
	
	
	//int resv_style; //Support FF style only
	FlowDescriptor_t* flow_descriptor_list = new FlowDescriptor_t[InLIST_SIZE];
		

	for(i=0;i<InLIST_SIZE;i++)
	{
	 flow_descriptor_list[i].Filter_Spec_Object.SrcAddress= 0;
	 flow_descriptor_list[i].Filter_Spec_Object.SrcPort=0;
	 flow_descriptor_list[i].Filter_Spec_Object.Lsp_Id=-1;

	 flow_descriptor_list[i].Flowspec_Object.link_delay=0;
	 flow_descriptor_list[i].Flowspec_Object.req_bandwidth=0;

	 flow_descriptor_list[i].label = -1;
	
	}
	
	outRM->setSession(&rsbEle->Session_Object);
	outRM->setStyle(rsbEle->style);

	outRM->setRTime(5);
	
	
	int peerInf=0;
	
	getPeerInet(PH, &peerInf);

	RsvpHopObj_t* hop= new RsvpHopObj_t;
	hop->Logical_Interface_Handle = peerInf;
	hop->Next_Hop_Address = PH;
	outRM->setHop(hop);     /*

	
	o    Select each sender PSB whose PHOP has address PH.  Set the
         local flag B_Merge off and execute the following steps.

         1.   Select all TCSB's whose Filter_spec_list's match the
              SENDER_TEMPLATE object in the PSB and whose OI appears
              in the OutInterface_list of the PSB.*/


	int pIndex =0;
	for(p_iterI = PSBList.begin(); p_iterI != PSBList.end(); p_iterI++)
	{
		p_iter = (PathStateBlock_t) *p_iterI;
		if(p_iter.Previous_Hop_Address != PH)
			continue;

		if((p_iter.Session_Object.DestAddress != rsbEle->Session_Object.DestAddress) ||
			(p_iter.Session_Object.DestPort != rsbEle->Session_Object.DestPort))
			continue;

		int c=0;
		for(c=0;c<InLIST_SIZE;c++)
			if(rsbEle->Filter_Spec_Object[c].SrcAddress == p_iter.Sender_Template_Object.SrcAddress &&
			 rsbEle->Filter_Spec_Object[c].SrcPort   == p_iter.Sender_Template_Object.SrcPort &&
			 rsbEle->Filter_Spec_Object[c].Lsp_Id == p_iter.Sender_Template_Object.Lsp_Id)
		{
			flow_descriptor_list[pIndex].Filter_Spec_Object = p_iter.Sender_Template_Object;
			//flow_descriptor_list[pIndex].Flowspec_Object = p_iter.Sender_Tspec_Object;
			flow_descriptor_list[pIndex].label = rsbEle->label[c];
			
			//Record the RRO
			int d;
			for(d=0;d < MAX_ROUTE;d++)
			{
				flow_descriptor_list[pIndex].RRO[d] = rsbEle->RRO[c][d];
			}
			for (d=0;d<MAX_ROUTE;d++)
				if(flow_descriptor_list[pIndex].RRO[d]==0)
				{
					flow_descriptor_list[pIndex].RRO[d]= my_id;
					break;
				}
			//End record RRO

			pIndex =pIndex+1;
			break;
		}
		if(c==InLIST_SIZE)
			continue;

		Tc_Flowspec->req_bandwidth = 0;
		Tc_Flowspec->link_delay = 0;

		
		for(t_iterI = TCSBList.begin(); t_iterI != TCSBList.end(); t_iterI++)
		{
			t_iter = (TrafficControlStateBlock_t) *t_iterI;
			found=OFF;
			for( int i=0; i<InLIST_SIZE; ++i )
			{
				if( (p_iter.Sender_Template_Object.SrcAddress != t_iter.Filter_Spec_Object[i].SrcAddress)  ||
				   (p_iter.Sender_Template_Object.SrcPort    != t_iter.Filter_Spec_Object[i].SrcPort))
					continue;

					found=ON;
					break;
					
			}
			if(found ==OFF)
				continue;

		
			found=OFF;
			if(t_iter.OI ==-1)
			{
					found=ON;
			}
			else if( p_iter.OutInterface_List==t_iter.OI  )
			{
					found=ON;
			}
				
			
			
			if( found==ON )
			{
				
	
				/*
				  3.   If B_Merge flag is off then ignore a blockaded TCSB,
                  as follows.

                  -    Select BSB's that match this TCSB.  If a selected
                       BSB is expired, delete it.  If any of the
                       unexpired BSB's has a Qb that is not strictly
                       larger than TC_Flowspec, then continue processing
                       with the next TCSB.*/

				//if( B_Merge==OFF )
				
					/* Ignore a blockaded TCSB */
					if ( t_iter.Fwd_Flowspec.req_bandwidth >
						 Tc_Flowspec->req_bandwidth )
					{
						Tc_Flowspec->req_bandwidth =
							t_iter.Fwd_Flowspec.req_bandwidth;
					}
					if ( t_iter.Fwd_Flowspec.link_delay >
						 Tc_Flowspec->link_delay )
					{
						Tc_Flowspec->link_delay =
							t_iter.Fwd_Flowspec.link_delay;
						
					}
				
			}
			
		}
	
	}

	//outRM->setLength(1); //Other small value so no fragmentation
	for(i=0;i<InLIST_SIZE;i++)
	{
	 flow_descriptor_list[i].Flowspec_Object = (*Tc_Flowspec);
	}

	outRM->setFlowDescriptor(flow_descriptor_list);
	
	
	ev << "Send RESV message to " << IPAddress(PH).getString() << "\n";
        ev << "RESV content is: \n";
        outRM->print();
	outRM->addPar("dest_addr")=IPAddress(PH).getString();
	
	

	//resvMsg->addPar("src_addr")=IPAddress(outRM->Rsvp_Hop_Object.Next_Hop_Address).getString();

	send(outRM, "to_ip");
}

void RSVP:: RTearFwd(ResvStateBlock_t *rsbEle, int PH)
{

	int PHOP;
	int found =OFF;
	int B_Merge=OFF;
	ResvTearMessage* outRM = new ResvTearMessage;
    FlowSpecObj_t* Tc_Flowspec = new FlowSpecObj_t;
    


	//int resv_style; //Support FF style only
	FlowDescriptor_t* flow_descriptor_list = new
		FlowDescriptor_t[InLIST_SIZE];
	
	for(int i=0;i<InLIST_SIZE;i++)
	{
	 flow_descriptor_list[i].Filter_Spec_Object =(rsbEle->Filter_Spec_Object[i]);
	 flow_descriptor_list[i].Flowspec_Object=(rsbEle->Flowspec_Object);
	}

    outRM->setFlowDescriptor(flow_descriptor_list);

	outRM->setSession(&rsbEle->Session_Object);
	
	
	int peerInf=0;
	
	getPeerInet(PH, &peerInf);

	RsvpHopObj_t* hop= new RsvpHopObj_t;
	hop->Logical_Interface_Handle = peerInf;
	hop->Next_Hop_Address = PH;
	outRM->setHop(hop); 
		ev << "Send RESV TEAR message to " << IPAddress(PH).getString() << "\n";
        ev << "RESV TEAR content is: \n";
        outRM->print();
	outRM->addPar("dest_addr")=IPAddress(PH).getString();
	
	
	send(outRM, "to_ip");



}


void RSVP::RemoveTrafficControl(ResvStateBlock_t *activeRSB)
{
	std::vector<TrafficControlStateBlock_t>::iterator t_iterI;
	TrafficControlStateBlock_t t_iter;
	TrafficControlStateBlock_t* tEle =NULL;
	bool found =false;

	for(t_iterI = TCSBList.begin(); t_iterI != TCSBList.end(); t_iterI++)
	{
		t_iter =(TrafficControlStateBlock_t)*t_iterI;
		if(t_iter.Session_Object.DestAddress == activeRSB->Session_Object.DestAddress &&
		   t_iter.Session_Object.Protocol_Id == activeRSB->Session_Object.Protocol_Id  &&
		   t_iter.Session_Object.DestPort    == activeRSB->Session_Object.DestPort    &&
		   t_iter.OI                         ==  activeRSB->OI)
		{
			for(int i=0; i<InLIST_SIZE; ++i)  {
				if(t_iter.Filter_Spec_Object[i].SrcAddress == activeRSB->Filter_Spec_Object[i].SrcAddress &&
				   t_iter.Filter_Spec_Object[i].SrcPort    == activeRSB->Filter_Spec_Object[i].SrcPort)

				{
					found=true;
					tEle = &t_iter;
					break;
				}
			}
		}
		if ( found)
            break;	
			
	}      
		
	if(tEle !=NULL)
	{
		int handle = tEle->Rhandle;
		std::vector<RHandleType_t>::iterator iterR;
		RHandleType_t iter1;
		bool handleFound =false;
  		int m;
		
		for( m=0; m<FlowTable.size(); m++)
		{
		if(FlowTable[m].handle == handle)
			{
			handleFound =true;	
			break;
			}
		}

		if(handleFound)
			FlowTable[m].handle =-1;

	}

	if(found)
	{
		ev << "Delete TCSB:\n";
		printTCSB(tEle);
		//TCSBList.erase(t_iterI);
		FlowSpecObj_t *fwdFS = new FlowSpecObj_t;
		//Release the resource to TED
		//GetFwdFS(activeRSB->OI, fwdFS) ;

	}

}

int
RSVP::UpdateTrafficControl(ResvStateBlock_t *activeRSB)
{
	std::vector<PathStateBlock_t>::iterator p_iterI;
	PathStateBlock_t p_iter;
    	std::vector<ResvStateBlock_t>::iterator r_iterI;
	ResvStateBlock_t r_iter;
    	std::vector<TrafficControlStateBlock_t>::iterator t_iterI;
	TrafficControlStateBlock_t t_iter;
	TrafficControlStateBlock_t* tEle;
	FlowSpecObj_t*	fwdFS = new FlowSpecObj_t;
	fwdFS->req_bandwidth =0;
	fwdFS->link_delay =0;
	
	FlowSpecObj_t* Tc_Flowspec = new FlowSpecObj_t;
	Tc_Flowspec->req_bandwidth =0;
	Tc_Flowspec->link_delay =0;
	int TC_E_Police_flag = OFF;
	int Resv_Refresh_Needed_Flag=OFF;
	


	FilterSpecObj_t TC_Filter_Spec[InLIST_SIZE];
	for(int m =0; m<InLIST_SIZE;m++)
	{
	TC_Filter_Spec[m].SrcAddress =0;
	TC_Filter_Spec[m].SrcPort =0;
	}
	
	FlowSpecObj_t* Path_Te = new FlowSpecObj_t;
	Path_Te->req_bandwidth = 0;
	Path_Te->link_delay = 0;
	int found = OFF;
	int handle =OFF;

	
	for(p_iterI = PSBList.begin(); p_iterI != PSBList.end(); p_iterI++)
	{
		p_iter = (PathStateBlock_t) *p_iterI;
		if(p_iter.Sender_Template_Object.SrcAddress==0)
			continue;
		if((p_iter.Session_Object.DestAddress != activeRSB->Session_Object.DestAddress) ||
			(p_iter.Session_Object.DestPort != activeRSB->Session_Object.DestPort))
			continue;

		for(int k =0;k< InLIST_SIZE;k++)
		{
		   if(p_iter.Sender_Template_Object.SrcAddress == activeRSB->Filter_Spec_Object[k].SrcAddress &&
		   p_iter.Sender_Template_Object.SrcPort    == activeRSB->Filter_Spec_Object[k].SrcPort)
		   {
		
				if( p_iter.OutInterface_List== 
								activeRSB->OI  )
				{
					if( p_iter.E_Police_Flag == ON )
					{
						TC_E_Police_flag = ON;
					}
					Path_Te->req_bandwidth +=
						p_iter.Sender_Tspec_Object.req_bandwidth;
					Path_Te->link_delay +=
						p_iter.Sender_Tspec_Object.link_delay;
					ev << "Update Path_Te for interface " <<
					IPAddress(activeRSB->OI).getString() << "\n";
					ev << "Bandwidth: " <<    Path_Te->req_bandwidth << "\n";
					ev << "Delay:     " <<    Path_Te->link_delay    << "\n";
				}
			
		   }
		}

	}
	int Is_Biggest=OFF;
	int inx =0;

	for( r_iterI = RSBList.begin(); r_iterI != RSBList.end(); r_iterI++ )
	{
		r_iter = (ResvStateBlock_t)*r_iterI;
		
		int i=0;
		
		for(i=0;i< InLIST_SIZE;i++)
		{
		   if((r_iter.Filter_Spec_Object[i].SrcAddress != activeRSB->Filter_Spec_Object[i].SrcAddress) ||
		      (r_iter.Filter_Spec_Object[i].SrcPort != activeRSB->Filter_Spec_Object[i].SrcPort ))
		      break;
        }
        if(i!=InLIST_SIZE)
             continue;


		if(r_iter.Session_Object.DestAddress == activeRSB->Session_Object.DestAddress &&
		   r_iter.Session_Object.Protocol_Id == activeRSB->Session_Object.Protocol_Id &&
		   r_iter.Session_Object.DestPort    == activeRSB->Session_Object.DestPort    &&
		   r_iter.OI                            == activeRSB->OI)
		{                                            /*	 
	            -    Compute the effective kernel flowspec,
                TC_Flowspec, as the LUB of the FLOWSPEC values in
                these RSB's.*/

			if( r_iter.Flowspec_Object.req_bandwidth > 
				Tc_Flowspec->req_bandwidth )
			{
				Tc_Flowspec->req_bandwidth = 
					r_iter.Flowspec_Object.req_bandwidth;
			}
			if( r_iter.Flowspec_Object.link_delay > Tc_Flowspec->link_delay )
			{
				Tc_Flowspec->link_delay = r_iter.Flowspec_Object.link_delay;
			}
			
			for(int k =0; k!=InLIST_SIZE;k++)
			if( r_iter.Filter_Spec_Object[k].SrcAddress !=0)
				TC_Filter_Spec[inx++] = r_iter.Filter_Spec_Object[k];
			
			ev << "Update Flowspec for interface: " << IPAddress(activeRSB->OI).getString() << "\n";
			ev << "Bandwidth: " <<    Tc_Flowspec->req_bandwidth << "\n";
			ev << "Delay:     " <<     Tc_Flowspec->link_delay << "\n";
			
		}
		
	}
		/*
	       -    If the active RSB has a FLOWSPEC larger than all
                the others, turn on the Is_Biggest flag.*/
	if( (activeRSB->Flowspec_Object.req_bandwidth >
		Tc_Flowspec->req_bandwidth) && (activeRSB->Flowspec_Object.link_delay >
		Tc_Flowspec->link_delay))
	{
		Is_Biggest = ON;
	}

	/*

	
	 o    Search for a TCSB matching SESSION and OI; for distinct
          style (FF), it must also match Filter_spec_list.

          If none is found, create a new TCSB.*/

	found =OFF;

	for(t_iterI = TCSBList.begin(); t_iterI != TCSBList.end(); t_iterI++)
	{
		t_iter =(TrafficControlStateBlock_t)*t_iterI;
		if(t_iter.Session_Object.DestAddress == activeRSB->Session_Object.DestAddress &&
		   t_iter.Session_Object.Protocol_Id == activeRSB->Session_Object.Protocol_Id  &&
		   t_iter.Session_Object.DestPort    == activeRSB->Session_Object.DestPort    &&
		   t_iter.OI                         ==  activeRSB->OI)
		{
			//for(int i=0; i<InLIST_SIZE; ++i)  {
				//if(t_iter.Filter_Spec_Object[i].SrcAddress == activeRSB->Filter_Spec_Object.SrcAddress &&
				   //t_iter.Filter_Spec_Object[i].SrcPort    == activeRSB->Filter_Spec_Object.SrcPort)
		

				{
					found=ON;
					tEle = &t_iter;
					break;
				}
			//}
		}
		
			
	}                       /*

	
	  o    If TCSB is new:

             1.   Store TC_Flowspec, TC_Filter_Spec*, Path_Te, and the
                  police flags into TCSB.*/

      

	if( found==OFF )
	{
		/* TCSB is new */
		TrafficControlStateBlock_t * newTcsb = new TrafficControlStateBlock_t;
		setSessionforTCSB(newTcsb,&activeRSB->Session_Object );
	
		newTcsb->OI = activeRSB->OI ;
		setTCFlowSpecforTCSB(newTcsb,Tc_Flowspec);
        setFilterSpecforTCSB(newTcsb,TC_Filter_Spec );

		setTCTspecforTCSB(newTcsb,Path_Te);
	
		newTcsb->E_Police_Flag = TC_E_Police_flag;
	    	Resv_Refresh_Needed_Flag=ON;
	    
	
		handle = TC_AddFlowspec( newTcsb->Session_Object.Tunnel_Id, 
			newTcsb->Session_Object.holdingPri, newTcsb->Session_Object.setupPri, 
			newTcsb->OI, newTcsb->TC_Flowspec,newTcsb->TC_Tspec, fwdFS );

		if(handle ==-1)
			return -1;

		newTcsb->Rhandle = handle;
		
		setFwdFlowSpecforTCSB(newTcsb,fwdFS);
		
		newTcsb->Fhandle = handle;

		TCSBList.push_back(*newTcsb);
		ev << "************************Adding new TCSB*********************\n";
		printTCSB(newTcsb);

		tEle = newTcsb;//TCSBList.end();
		
	}
	else
	{

		ev << "Found a matching TCSB\n";
		if((tEle->TC_Flowspec.req_bandwidth == Tc_Flowspec->req_bandwidth &&
            tEle->TC_Flowspec.link_delay    == Tc_Flowspec->link_delay)   ||
		   (tEle->TC_Tspec.req_bandwidth     == Path_Te->req_bandwidth     &&
			tEle->TC_Tspec.link_delay       == Path_Te->link_delay)       ||
			tEle->E_Police_Flag            != TC_E_Police_flag)
		{
			if((tEle->TC_Flowspec.req_bandwidth == Tc_Flowspec->req_bandwidth &&
            tEle->TC_Flowspec.link_delay    == Tc_Flowspec->link_delay)   ||
		   (tEle->TC_Tspec.req_bandwidth     == Path_Te->req_bandwidth     &&
			tEle->TC_Tspec.link_delay       == Path_Te->link_delay)   )
			{
				Resv_Refresh_Needed_Flag=ON;
			}
		
			handle = TC_ModFlowspec(tEle->Session_Object.Tunnel_Id,tEle->OI,
				tEle->TC_Flowspec, tEle->TC_Tspec, &tEle->Fwd_Flowspec );

			if(handle ==-1)
				return -1;
		}

		
		ev << "Finish Found a matching TCSB\n";
	}
	if ( Is_Biggest == ON )
	{
		tEle->Receiver_Address = activeRSB->Receiver_Address;
		Resv_Refresh_Needed_Flag=ON;
	}
	else
	{
		/* Create and send a ResvConf message to the address */
	}
	return 0;


}

int 
RSVP::TC_AddFlowspec(int tunnelId, int holdingPri, int setupPri,  int OI, 
					 FlowSpecObj_t fs, SenderTspecObj_t ts, FlowSpecObj_t *fwdFS)
{
	
	//int handle=tunnelId;
	//if(!FlowTable.empty())
	//handle = FlowTable.size();

	FlowSpecObj_t* addFs = new FlowSpecObj_t;
	if(fs.req_bandwidth > ts.req_bandwidth)
		addFs->req_bandwidth = ts.req_bandwidth;
	else
		addFs->req_bandwidth = fs.req_bandwidth;
	addFs->link_delay = fs.link_delay;

	if(AllocateResource(tunnelId, holdingPri, setupPri, OI, *addFs))
	{
		
		RHandleType_t* rHandle = new RHandleType_t;
		rHandle->isfull =1;
		rHandle->OI =OI;
		rHandle->TC_Flowspec = fs;
		rHandle->Path_Te = ts;
		rHandle->handle = tunnelId;
		rHandle->holdingPri = holdingPri;
		rHandle->setupPri = setupPri;
		
		FlowTable.push_back(*rHandle);
		GetFwdFS(OI, fwdFS);
		return tunnelId;
	}

	/*
	{
		FlowTable.pop_back();
		fwdFS=NULL;
		return -1;
	}*/

	return -1;
}



int
RSVP::TC_ModFlowspec(int tunnelId,int OI,  FlowSpecObj_t fs, SenderTspecObj_t ts,
				FlowSpecObj_t *fwdFS)
{

	double oldBWRequest=0;
	FlowSpecObj_t* addFs = new FlowSpecObj_t;
	if(fs.req_bandwidth > ts.req_bandwidth)
		addFs->req_bandwidth = ts.req_bandwidth;
	else
		addFs->req_bandwidth = fs.req_bandwidth;
	addFs->link_delay = fs.link_delay;

	
	for(int i=0; i< FlowTable.size(); i++)
	{
		if(FlowTable[i].handle == tunnelId)
		{
			if(FlowTable[i].TC_Flowspec.req_bandwidth > FlowTable[i].Path_Te.req_bandwidth)
				oldBWRequest = FlowTable[i].Path_Te.req_bandwidth;
			else
				oldBWRequest =FlowTable[i].TC_Flowspec.req_bandwidth;
			
		   addFs->req_bandwidth = (addFs->req_bandwidth) - oldBWRequest;

	   		if(AllocateResource(tunnelId, FlowTable[i].holdingPri, FlowTable[i].setupPri, OI, *addFs))
			{
				
				FlowTable[i].isfull =1;
				FlowTable[i].OI =OI;
				FlowTable[i].TC_Flowspec = fs;
				FlowTable[i].Path_Te = ts;
				FlowTable[i].handle = tunnelId;
				//FlowTable[i].holdingPri = holdingPri;
				//FlowTable[i].setupPri = setupPri;
				
				GetFwdFS(OI, fwdFS);
				return tunnelId;
			}
			else
			{
			  //FlowTable.erase(FlowTable.begin() + i);
			  FlowTable[i].handle =-1;
			  FlowTable[i].OI =-1;
			
			  return -1;
			}
		}
	}


		return 0;
}

bool RSVP::AllocateResource(int tunnelId, int holdingPri, int setupPri, int oi, FlowSpecObj_t fs)
{
	//Check if this fs is permissible
	ev << "********************ENTER ALLOCATE RESOURCE*******************************\n";
	ev << "Try to allocate resource for TE Tunnel with (Id, holdingPri, setupPri)=(" <<
		tunnelId << "," << holdingPri << "," << setupPri << ")\n";

 
	//bool findTunnel = false;
	double requestBW =0;
	double total_releasedBW=0;
	double releasedBW=0;

    for (int i=0; i< ted.size(); i++)
    {
		if(ted[i].local.getInt() == oi &&
		                  ted[i].advrouter.getInt() ==my_id)
		{
			//Note: UnRB[7] <= UnRW[setupPri] <= UnRW[holdingPri] <= BW[0]
			//UnRW[7] is the actual BW left on the link

			//If there is enough resource
			if(ted[i].UnResvBandwith[0] >= fs.req_bandwidth)
			{
				for(int p = holdingPri; p< 8; p++)
				{
					ted[i].UnResvBandwith[p] -=fs.req_bandwidth;
				}
				return true;
			}
			else //Start pre-emption process
			{
				requestBW = fs.req_bandwidth - ted[i].UnResvBandwith[0];
				ev << "Need an addional amount of BW=" << requestBW << "\n";
				ev << "Check whether the reservation is possible\n";
			
				int lowest_pri_to_preempt;
				bool isFeasible = false;

				total_releasedBW =0;

				//Check for resource release from lowest holding priority(7) to highest holding priority(0)
				//Sessions with the same holding priority is selected based on the longest operation time
				for(lowest_pri_to_preempt = 7; lowest_pri_to_preempt>=setupPri;lowest_pri_to_preempt--)
				{
					//findTunnel = false;
					//Select which tunnel to preempt
						
					for(int j=0; j< FlowTable.size(); j++)
					{
						//Ignore myself
						if(FlowTable[i].handle == tunnelId)
							continue;

						releasedBW = FlowTable[j].TC_Flowspec.req_bandwidth;
						if(FlowTable[j].TC_Flowspec.req_bandwidth > FlowTable[j].Path_Te.req_bandwidth)
							releasedBW =FlowTable[j].Path_Te.req_bandwidth;

						total_releasedBW = total_releasedBW + releasedBW;

						
						if(total_releasedBW >= requestBW)
						{
						
							//Preempt this tunnel
							/*
							ev << "Going to preempt TE tunnel with ID = " << FlowTable[j].handle;
							ev << ", its holdingPri=" << FlowTable[j].holdingPri << "\n";

							findTunnel = true;
							*/
							isFeasible =true;
							break;
						}
					}
					if(isFeasible)
						break;

				}
				if(total_releasedBW < requestBW)
				{
					ev << "Cannot allocate enough resource\n";
					ev << "Request denied\n";
					return false;
				}
				//Hold value of lowest_pri_preempt and j ?

				//Starting the preemtion process
				isFeasible =false;
				total_releasedBW=0;
				releasedBW=0;


				for(lowest_pri_to_preempt = 7; lowest_pri_to_preempt>=setupPri;lowest_pri_to_preempt--)
				{			
					for(int j=0; j< FlowTable.size(); j++)
					{
						//Ignore myself
						if(FlowTable[i].handle == tunnelId)
							continue;

						releasedBW = FlowTable[j].TC_Flowspec.req_bandwidth;
						if(FlowTable[j].TC_Flowspec.req_bandwidth > FlowTable[j].Path_Te.req_bandwidth)
							releasedBW =FlowTable[j].Path_Te.req_bandwidth;

						total_releasedBW = total_releasedBW + releasedBW;

						//Preempt this tunnel	
						ev << "Going to preempt TE tunnel with ID = " << FlowTable[j].handle;
						ev << ", its holdingPri=" << FlowTable[j].holdingPri << "\n";
						ev << "Release an amount of BW =" << releasedBW << "\n";
						preemptTunnel(FlowTable[i].handle);
						for(int p = 7; p >=lowest_pri_to_preempt; p++)
						{
							ted[i].UnResvBandwith[p] +=releasedBW;
						}

						if(total_releasedBW >= requestBW)
						{
							ev << "Release enough resource, stop\n";
							
							isFeasible =true;
							break;
						}
					}
					if(isFeasible)
						break;

				}
				ev << "Do the resevation\n";
				for(int p=7; p >=lowest_pri_to_preempt; p++)
				ted[i].UnResvBandwith[p]-= requestBW;

				propagateTEDchanges();
				return true;

			}
			
		
		}
	}
	return true;
}

int
RSVP::GetFwdFS(int oi, FlowSpecObj_t *fwdFS) 
{
	
	std::vector<RHandleType_t>::iterator iterR;
	RHandleType_t iter;
	 std::vector<telinkstate>::iterator ted_iterI;
	telinkstate ted_iter;
	double req_BW=0;

	
	for(iterR = FlowTable.begin(); iterR != FlowTable.end(); iterR++)
	{
	iter = (RHandleType_t)*iterR;
	if(iter.OI ==oi)
	  {
	  fwdFS->req_bandwidth += iter.TC_Flowspec.req_bandwidth;
	  fwdFS->link_delay += iter.TC_Flowspec.link_delay;
	  }
	
	}




	return 1;
}


void 
RSVP::Mcast_Route_Query(int sa, int iad, int da, int *outl)
{

	if(da == my_id)
	{
		(*outl) =-1;
		return;
	}
	
	int foundIndex;
	int j=0;
	
	foundIndex = rt->outputPortNoNew(IPAddress(da));
	(*outl) = rt->getInterfaceByIndex(foundIndex)->inetAddr->getInt();

	return;
}


void RSVP::findRoutingTable()

{

  	cObject *foundmod;

	cModule *curmod = this;


	// find LIB Table

	rt = NULL;

	for (curmod = parentModule(); curmod != NULL;
			curmod = curmod->parentModule())
	{

		if ((foundmod = curmod->findObject("routingTable", false)) != NULL)
		{

			rt = (RoutingTable *)foundmod;

			break;

		}

	}

	

	if(rt==NULL)
	ev << "Error occurs - Fail to find routing table" << "\n";

	else

	ev << "Routing Table found succesfully" << "\n";

}

bool RSVP::IsLocalAddress(int addr)
{
	for(int i=0;i<InLIST_SIZE;i++)
		if(LocalAddress[i] ==addr)
			return true;

		return false;
}


void RSVP::updateTED()
{
    
	// find TED

    cTopology topo;
    topo.extractByModuleType( "TED", NULL );
    
    sTopoNode *node = topo.node(0);
    TED *module = (TED*)(node->module());
    ted = *(module->getTED());
    
}

void RSVP::getPeerIPAddress(int peerInf, int* peerIP)
{
 std::vector<telinkstate>::iterator ted_iterI;
	telinkstate ted_iter;


	updateTED();
    for (ted_iterI=ted.begin(); ted_iterI != ted.end(); ted_iterI++)
    {
		ted_iter = (telinkstate)*ted_iterI;
	if((ted_iter.local.getInt() == peerInf) &&
				(ted_iter.advrouter.getInt()==my_id))
				{
	(*peerIP) = ted_iter.linkid.getInt();
	break;
	}
			
	
     }
}

void RSVP::getPeerInet(int peerIP, int* peerInf)
{    std::vector<telinkstate>::iterator ted_iterI;
	telinkstate ted_iter;


	updateTED();
    for (ted_iterI=ted.begin(); ted_iterI != ted.end(); ted_iterI++)
    {
		ted_iter = (telinkstate)*ted_iterI;
	if((ted_iter.linkid.getInt() == peerIP) &&
				(ted_iter.advrouter.getInt()==my_id))
				{
	(*peerInf) = ted_iter.remote.getInt();
	break;
	}
			
	
     }
}

void RSVP::getIncInet(int peerIP, int* incInet)
{
   std::vector<telinkstate>::iterator ted_iterI;
	telinkstate ted_iter;


	updateTED();
    for (ted_iterI=ted.begin(); ted_iterI != ted.end(); ted_iterI++)
    {
		ted_iter = (telinkstate)*ted_iterI;
	if((ted_iter.linkid.getInt() == peerIP) &&
				(ted_iter.advrouter.getInt()==my_id))
				{
	(*incInet) = ted_iter.local.getInt();
	break;
	}
			
	
     }
}
void RSVP::getPeerIPAddress(int dest, int* peerIP, int* peerInf)
{
	int outl=0;
	Mcast_Route_Query(0, 0, dest, &outl);
	std::vector<telinkstate>::iterator ted_iterI;
	telinkstate ted_iter;

	updateTED();
    for (ted_iterI=ted.begin(); ted_iterI != ted.end(); ted_iterI++)
    {
		ted_iter = (telinkstate)*ted_iterI;
	if((ted_iter.local.getInt() == outl) && (ted_iter.advrouter.getInt()==my_id)) 	
		{
			 *peerIP=ted_iter.linkid.getInt();
			 *peerInf = ted_iter.remote.getInt();
			 break;
			
		}
     }
	
	
	

}


// DEBUG UTILITIES

void RSVP::printSessionObject(SessionObj_t* s)
{
ev << "Session: (destAddr, prot_id, destPort, setupPri, holdingPri, Tunnel_Id, XTunnel_Id) = ("<<
		IPAddress(s->DestAddress).getString() << "," <<
 		s->Protocol_Id << "," << s->DestPort << 
		"," << s->setupPri << "," << s->holdingPri <<
		"," << s->Tunnel_Id << "," << s->Extended_Tunnel_Id << ")\n";

}

void RSVP::printRSVPHopObject(RsvpHopObj_t* r)
{
ev << "RSVP HOP: (NextHopAddress, LogicalInterfaceHandle) = (" <<
	IPAddress(r->Next_Hop_Address).getString() << "," <<
	IPAddress(r->Logical_Interface_Handle) << ")\n";
}

void RSVP::printSenderTemplateObject(SenderTemplateObj_t* s)
{
	if((s->SrcAddress) !=0)
	{
		ev << "SenderTemplate: (srcAddr, srcPort)=(" <<
		IPAddress(s->SrcAddress).getString() << "," <<
		s->SrcPort <<
		"," << s->Lsp_Id << ")\n";
	}
}

void RSVP::printSenderTspecObject(SenderTspecObj_t* s)
{
ev << "SenderTspec: (req_bandwidth, link_delay)= (" <<
s->req_bandwidth << "," << s->link_delay << ")\n";

}

void RSVP::printSenderDescriptorObject(SenderDescriptor_t* s)
{
printSenderTemplateObject(&s->Sender_Template_Object);
printSenderTspecObject(&s->Sender_Tspec_Object);
}

void RSVP::printFlowDescriptorListObject(FlowDescriptor_t* f)
{

}

void RSVP::printPSB(PathStateBlock_t* p)
{
  printSessionObject(&p->Session_Object);
  printSenderTemplateObject(&p->Sender_Template_Object);
  printSenderTspecObject(&p->Sender_Tspec_Object);
  ev << "Previous Hop Address = " <<
  IPAddress(p->Previous_Hop_Address).getString() << "\n";
  ev << "Logical Interface Handle=" <<
  IPAddress(p->LIH).getString() << "\n";
  ev << "Out Interface List = " <<
  IPAddress(p->OutInterface_List) << "\n";



}

void RSVP::printRSB(ResvStateBlock_t* r)
{
	  printSessionObject(&r->Session_Object);
	  ev << "Next Hop Address=" <<IPAddress(r->Next_Hop_Address).getString() <<
	"\n";
	  ev << "OI = " << IPAddress(r->OI).getString() << "\n";


printSenderTemplateObject((SenderTemplateObj_t*)(&r->Filter_Spec_Object));
printSenderTspecObject((SenderTspecObj_t*)(&r->Flowspec_Object));

}


void RSVP::setSessionforTCSB(TrafficControlStateBlock_t* t, SessionObj_t* s)
{
	SessionObj_t* nS = new SessionObj_t;

	nS->DestAddress = s->DestAddress;
	nS->Protocol_Id= s->Protocol_Id;
	nS->DestPort = s->DestPort;
	nS->setupPri = s->setupPri;
	nS->holdingPri = s->holdingPri;
	nS->Tunnel_Id = s->Tunnel_Id;
	nS->Extended_Tunnel_Id = s->Extended_Tunnel_Id;
	t->Session_Object = *nS;

}

void RSVP::setFilterSpecforTCSB(TrafficControlStateBlock_t* t,
						 FilterSpecObj_t *f)
{

for(int i=0; i<InLIST_SIZE;i++)
	{
	t->Filter_Spec_Object[i].SrcAddress = (*(f+i)).SrcAddress;
	t->Filter_Spec_Object[i].SrcPort =(*(f+i)).SrcPort;
	t->Filter_Spec_Object[i].Lsp_Id =(*(f+i)).Lsp_Id;
	}

}


void RSVP::setTCFlowSpecforTCSB(TrafficControlStateBlock_t* t,
				FlowSpecObj_t *f)
{

FlowSpecObj_t* nF = new FlowSpecObj_t;
nF->req_bandwidth = f->req_bandwidth;
nF->link_delay = f->link_delay;

t->TC_Flowspec = *nF;

}

void  RSVP::setFwdFlowSpecforTCSB(TrafficControlStateBlock_t* t,
				FlowSpecObj_t* f)
{
	FlowSpecObj_t* nF = new FlowSpecObj_t;
	nF->req_bandwidth = f->req_bandwidth;
	nF->link_delay = f->link_delay;

	t->Fwd_Flowspec = *nF;

}

void RSVP::setTCTspecforTCSB(TrafficControlStateBlock_t* t,
				SenderTspecObj_t* s)
{
	SenderTspecObj_t* nS = new SenderTspecObj_t;
	nS->req_bandwidth = s->req_bandwidth;
	nS->link_delay = s->link_delay;

	t->TC_Tspec = *nS;
}

void RSVP::printTCSB(TrafficControlStateBlock_t* t)
{
	printSessionObject(&t->Session_Object);
	ev << "OI=" <<IPAddress(t->OI).getString() << "\n";
	ev << "Filter spec list:\n";
	for(int i=0;i<InLIST_SIZE;i++)
	{
	if( (*((t->Filter_Spec_Object) +i)).SrcAddress !=0)
	printSenderTemplateObject((SenderTemplateObj_t*)((t->Filter_Spec_Object) +i));
	}
	ev << "TC Flowspec:\n";
	printSenderTspecObject(&t->TC_Flowspec);
	ev << "Forward Flowspec:\n";
	printSenderTspecObject(&t->Fwd_Flowspec);

	printSenderTspecObject(&t->TC_Tspec);


}


void RSVP::findOSPFTE()
{
  cObject *foundmod;

	cModule *curmod = this;

	ospfte = NULL;

	for (curmod = parentModule(); curmod != NULL;
			curmod = curmod->parentModule())
	{

		if ((foundmod = curmod->findObject("ospf_te", false)) != NULL)
		{

			ospfte = (OspfTe *)foundmod;

			break;

		}

	}

	

	if(ospfte==NULL)
	ev << "Error occurs - Fail to find OSPFTE" << "\n";

	else
	ev << "OSPFTE found succesfully" << "\n";

}

bool RSVP::doCACCheck(PathMessage* pmsg, int OI)
{


//	if(PSBList.empty())
//	{
//		ev << "First PATH reservation. Successful\n";
//		return true;
//	}

    std::vector<PathStateBlock_t>::iterator p_iterI;
	PathStateBlock_t		p_iter;


	double requestBW =pmsg->getBW();
	double my_requestBW = pmsg->getBW();
	int setupPri = pmsg->getSetupPri();


	 for(p_iterI = PSBList.begin(); p_iterI != PSBList.end(); p_iterI++)
	 {
		 p_iter = (PathStateBlock_t)*p_iterI;

		 //Do CAC on the OI
		 if(p_iter.OutInterface_List != OI)
			 continue;

		 //Only PSB that reserve the resource but not get actual allocation
		 if(p_iter.SAllocated)
			 continue;
		 
		 //Don't count the request for the same session twice
		 if(p_iter.Session_Object.DestAddress == pmsg->getDestAddress() &&
			 p_iter.Session_Object.DestPort == pmsg->getDestPort() &&
			 p_iter.Sender_Template_Object.SrcAddress == pmsg->getSrcAddress() &&
			 p_iter.Sender_Template_Object.SrcPort== pmsg->getSrcPort())
		 {
			 if(p_iter.Sender_Tspec_Object.req_bandwidth > my_requestBW)
			 {
				 requestBW = requestBW + p_iter.Sender_Tspec_Object.req_bandwidth - my_requestBW;
				 my_requestBW = p_iter.Sender_Tspec_Object.req_bandwidth;
			 }
		 }
		 else
		 {
			 requestBW = requestBW + p_iter.Sender_Tspec_Object.req_bandwidth;
		 }

	 }
	 updateTED();
	 
	 for (int k=0; k < ted.size(); k++)
	 {
	
		if(ted[k].local.getInt() == OI && ted[k].advrouter.getInt() ==my_id)
		{
			ev << "Check reserve BW = " << requestBW << "for link(" <<
					ted[k].advrouter.getString() << "," << ted[k].linkid.getString() << ")\n"; 
			ev << "BW available = " << ted[k].UnResvBandwith[setupPri] << "\n";
			ev << "Requests for PATHS on this link =" << requestBW << "\n";

			if(ted[k].UnResvBandwith[setupPri] >= requestBW)
			{		
			
				ev << "Admission Control passes for PATH message\n";
				//ted[k].UnResvBandwith[0] = ted[k].UnResvBandwith[0] - requestBW;
				return true;
			}
			else
			{
				ev << "Admission Control fails for PATH message\n";
				//Contruct new PATH ERROR and send back
				PathErrorMessage* pe = new PathErrorMessage();
				pe->setErrorCode(1); //Admission Control Error
				pe->setErrorNode(my_id);
				pe->setSession(pmsg->getSession());
				pe->setSenderTemplate(pmsg->getSenderTemplate());
				pe->setSenderTspec(pmsg->getSenderTspec());
				pe->addPar("src_addr") = IPAddress(my_id).getString();
				pe->addPar("dest_addr") = IPAddress(pmsg->getNHOP()).getString();

				ev << "Propagate PATH ERROR to " << IPAddress(pmsg->getNHOP()).getString() <<"\n";
				send(pe, "to_ip");
				return false;
			}
		
		}
	}
	
	 return true;

}

void RSVP::preemptTunnel(int tunnelId)
{
ev << "*******************ENTER PREEMPT TUNNEL ************************************\n";
//Send PATH TEAR and RESV TEAR to both directions to tear off the current reservation

//PATH TEAR
std::vector<PathStateBlock_t>::iterator p_iterI;
PathStateBlock_t p_iter;

std::vector<ResvStateBlock_t>::iterator r_iterI;
ResvStateBlock_t r_iter;

std::vector<int> locList;

   if(!PSBList.empty())
   {
	  for(int m=0; m< PSBList.size();m++)
	  {
		if(PSBList[m].Session_Object.Tunnel_Id == tunnelId)
		{
			PathTearMessage* ptMsg = new PathTearMessage();
			ptMsg->setSession(&PSBList[m].Session_Object);
			ptMsg->setSenderTemplate(&PSBList[m].Sender_Template_Object);

			locList.push_back(PSBList[m].Previous_Hop_Address);
						   
			if(!IsER)
			{
				int peerIP =0;
				getPeerIPAddress(PSBList[m].OutInterface_List, &peerIP);
				ptMsg->addPar("dest_addr") = IPAddress(peerIP).getString();
				ptMsg->addPar("src_addr") = IPAddress(my_id).getString();

				ev << "Sending PATH TEAR MESSAGE to " << IPAddress(peerIP).getString();
				send(ptMsg, "to_ip");
			}

			//PSBList.erase(p_iterI); //This line is very unsafe !!!!
			PSBList[m].Session_Object.DestAddress =0;
			PSBList[m].Session_Object.DestPort =0;

		}
	  }
   }


   
//RESV TEAR
   if(!RSBList.empty())
   {
	   for(int m=0; m< PSBList.size();m++)
	   {
		   if(PSBList[m].Session_Object.Tunnel_Id == tunnelId)
		   {
			   for(int i=0;i< locList.size();i++)
				   RTearFwd(&r_iter, locList[i]);
			   //RSBList.erase(r_iterI);//This line is very unsafe !!!!
			   PSBList[m].Session_Object.DestAddress =0;
			   PSBList[m].Session_Object.DestPort =0;

		   }
	   }
   }


}


//Note: Currently the OSPF TED is partly implemented and this is needed as 
//an alternative for the routing table to convergece over the network
void RSVP::propagateTEDchanges()
{
		// find TED
    cTopology topo;
    topo.extractByModuleType( "TED", NULL );
	TED* myTED;
    
    sTopoNode *node = topo.node(0);
    myTED = (TED*)(node->module());
    if(myTED == NULL)
    {
		ev << "Tester Error: Fail to locate TED\n";
		return;
    }
	std::vector<telinkstate>	*originalTED;
	originalTED = myTED->getTED();

	originalTED->clear();

	for(int i=0; i< ted.size();i++)
	{
		originalTED->push_back(ted[i]);
		
	}

}
				
				
				
				
				
				
				






